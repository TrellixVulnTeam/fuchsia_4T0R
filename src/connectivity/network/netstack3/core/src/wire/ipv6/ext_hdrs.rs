// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing of IPv6 Extension Headers.

use std::convert::TryFrom;
use std::marker::PhantomData;

use byteorder::{ByteOrder, NetworkEndian};
use packet::BufferView;
use zerocopy::LayoutVerified;

use crate::ip::{IpProto, Ipv6Addr, Ipv6ExtHdrType};
use crate::wire::util::records::{
    LimitedRecords, LimitedRecordsImpl, LimitedRecordsImplLayout, Records, RecordsContext,
    RecordsImpl, RecordsImplLayout,
};

/// An IPv6 Extension Header.
#[derive(Debug)]
pub(crate) struct Ipv6ExtensionHeader<'a> {
    // Marked as `pub(super)` because it is only used in tests within
    // the `crate::wire::ipv6` (`super`) module.
    pub(super) next_header: u8,
    data: Ipv6ExtensionHeaderData<'a>,
}

impl<'a> Ipv6ExtensionHeader<'a> {
    pub(crate) fn data(&self) -> &Ipv6ExtensionHeaderData<'a> {
        &self.data
    }
}

/// The data associated with an IPv6 Extension Header.
#[derive(Debug)]
pub(crate) enum Ipv6ExtensionHeaderData<'a> {
    HopByHopOptions { options: Records<&'a [u8], HopByHopOptionsImpl> },
    Routing { routing_data: RoutingData<'a> },
    Fragment { fragment_data: FragmentData<'a> },
    DestinationOptions { options: Records<&'a [u8], DestinationOptionsImpl> },
}

//
// Records parsing for IPv6 Extension Header
//

/// Possible errors that can happen when parsing IPv6 Extension Headers.
#[derive(Debug)]
pub(crate) enum Ipv6ExtensionHeaderParsingError {
    // `pointer` is the offset from the beginning of the first extension header
    // to the point of error. `must_send_icmp` is a flag that requires us to send
    // an ICMP response if true. `header_len` is the size of extension headers before
    // encountering an error (number of bytes from successfully parsed
    // extension headers).
    ErroneousHeaderField {
        pointer: u32,
        must_send_icmp: bool,
        header_len: usize,
    },
    UnrecognizedNextHeader {
        pointer: u32,
        must_send_icmp: bool,
        header_len: usize,
    },
    UnrecognizedOption {
        pointer: u32,
        must_send_icmp: bool,
        header_len: usize,
        action: ExtensionHeaderOptionAction,
    },
    BufferExhausted,
    MalformedData,
}

/// Context that gets passed around when parsing IPv6 Extension Headers.
#[derive(Debug, Clone)]
pub(crate) struct Ipv6ExtensionHeaderParsingContext {
    // Next expected header.
    // Marked as `pub(super)` because it is inly used in tests within
    // the `crate::wire::ipv6` (`super`) module.
    pub(super) next_header: u8,

    // Whether context is being used for iteration or not.
    iter: bool,

    // Counter for number of extension headers parsed.
    headers_parsed: usize,

    // Byte count of successfully parsed extension headers.
    bytes_parsed: usize,
}

impl Ipv6ExtensionHeaderParsingContext {
    pub(crate) fn new(next_header: u8) -> Ipv6ExtensionHeaderParsingContext {
        Ipv6ExtensionHeaderParsingContext {
            iter: false,
            headers_parsed: 0,
            next_header,
            bytes_parsed: 0,
        }
    }
}

impl RecordsContext for Ipv6ExtensionHeaderParsingContext {
    fn clone_for_iter(&self) -> Self {
        let mut ret = self.clone();
        ret.iter = true;
        ret
    }
}

/// Implement the actual parsing of IPv6 Extension Headers.
#[derive(Debug)]
pub(crate) struct Ipv6ExtensionHeaderImpl;

impl Ipv6ExtensionHeaderImpl {
    /// Make sure a Next Header value in an extension header is valid.
    fn valid_next_header(next_header: u8) -> bool {
        // Passing false to `is_valid_next_header`'s `for_fixed_header` parameter because
        // this function will never be called when checking the Next Header field
        // of the fixed header (which would be the first Next Header).
        is_valid_next_header(next_header, false)
    }

    /// Get the first two bytes if possible and return them.
    ///
    /// `get_next_hdr_and_len` takes the first two bytes from `data` and
    /// treats them as the Next Header and Hdr Ext Len fields. With the
    /// Next Header field, `get_next_hdr_and_len` makes sure it is a valid
    /// value before returning the Next Header and Hdr Ext Len fields.
    fn get_next_hdr_and_len<'a, BV: BufferView<&'a [u8]>>(
        data: &mut BV,
        context: &Ipv6ExtensionHeaderParsingContext,
    ) -> Result<(u8, u8), Ipv6ExtensionHeaderParsingError> {
        let next_header = data
            .take_front(1)
            .map(|x| x[0])
            .ok_or_else(|| Ipv6ExtensionHeaderParsingError::BufferExhausted)?;

        // Make sure we recognize the next header.
        // When parsing headers, if we encounter a next header value we don't
        // recognize, we SHOULD send back an ICMP response. Since we only SHOULD,
        // we set `must_send_icmp` to `false`.
        if !Self::valid_next_header(next_header) {
            return Err(Ipv6ExtensionHeaderParsingError::UnrecognizedNextHeader {
                pointer: context.bytes_parsed as u32,
                must_send_icmp: false,
                header_len: context.bytes_parsed,
            });
        }

        let hdr_ext_len = data
            .take_front(1)
            .map(|x| x[0])
            .ok_or_else(|| Ipv6ExtensionHeaderParsingError::BufferExhausted)?;

        Ok((next_header, hdr_ext_len))
    }

    /// Parse Hop By Hop Options Extension Header.
    // TODO(ghanan): Look into implementing the IPv6 Jumbo Payload option
    //               (https://tools.ietf.org/html/rfc2675) and the router
    //               alert option (https://tools.ietf.org/html/rfc2711).
    fn parse_hop_by_hop_options<'a, BV: BufferView<&'a [u8]>>(
        data: &mut BV,
        context: &mut Ipv6ExtensionHeaderParsingContext,
    ) -> Result<Option<Option<Ipv6ExtensionHeader<'a>>>, Ipv6ExtensionHeaderParsingError> {
        let (next_header, hdr_ext_len) = Self::get_next_hdr_and_len(data, context)?;

        // As per RFC 8200 section 4.3, Hdr Ext Len is the length of this extension
        // header in  8-octect units, not including the first 8 octets (where 2 of
        // them are the Next Header and the Hdr Ext Len fields). Since we already
        // 'took' the Next Header and Hdr Ext Len octets, we need to make sure
        // we have (Hdr Ext Len) * 8 + 6 bytes bytes in `data`.
        let expected_len = (hdr_ext_len as usize) * 8 + 6;

        let options = data
            .take_front(expected_len)
            .ok_or_else(|| Ipv6ExtensionHeaderParsingError::BufferExhausted)?;

        let options_context = ExtensionHeaderOptionContext::new();
        let options = Records::parse_with_context(options, options_context).map_err(|e| {
            // We know the below `try_from` call will not result in a `None` value because
            // the maximum size of an IPv6 packet's payload (extension headers + body) is
            // `std::u32::MAX`. This maximum size is only possible when using IPv6
            // jumbograms as defined by RFC 2675, which uses a 32 bit field for the payload
            // length. If we receive such a hypothetical packet with the maximum possible
            // payload length which only contains extension headers, we know that the offset
            // of any location within the payload must fit within an `u32`. If the packet is
            // a normal IPv6 packet (not a jumbogram), the maximum size of the payload is
            // `std::u16::MAX` (as the normal payload length field is only 16 bits), which
            // is significantly less than the maximum possible size of a jumbogram.
            ext_hdr_opt_err_to_ext_hdr_err(
                u32::try_from(context.bytes_parsed + 2).unwrap(),
                context.bytes_parsed,
                e,
            )
        })?;

        // Update context
        context.next_header = next_header;
        context.headers_parsed += 1;
        context.bytes_parsed += 2 + expected_len;

        return Ok(Some(Some(Ipv6ExtensionHeader {
            next_header,
            data: Ipv6ExtensionHeaderData::HopByHopOptions { options },
        })));
    }

    /// Parse Routing Extension Header.
    fn parse_routing<'a, BV: BufferView<&'a [u8]>>(
        data: &mut BV,
        context: &mut Ipv6ExtensionHeaderParsingContext,
    ) -> Result<Option<Option<Ipv6ExtensionHeader<'a>>>, Ipv6ExtensionHeaderParsingError> {
        // All routing extension headers (regardless of type) will have
        // 4 bytes worth of data we need to look at.
        let (next_header, hdr_ext_len) = Self::get_next_hdr_and_len(data, context)?;
        let routing_data =
            data.take_front(2).ok_or_else(|| Ipv6ExtensionHeaderParsingError::BufferExhausted)?;;
        let routing_type = routing_data[0];
        let segments_left = routing_data[1];

        // RFC2460 section 4.4 only defines a routing type of 0
        if routing_type != 0 {
            // If we receive a routing header with a non 0 router type,
            // what we do depends on the segments left. If segments left is
            // 0, we must ignore the routing header and continue processing
            // other headers. If segments left is not 0, we need to discard
            // this packet and send an ICMP Parameter Problem, Code 0 with a
            // pointer to this unrecognized routing type.
            if segments_left == 0 {
                return Ok(Some(None));
            } else {
                // As per RFC 8200, if we encounter a routing header with an unrecognized
                // routing type, and segments left is non-zero, we MUST discard the packet
                // and send and ICMP Parameter Problem response.
                return Err(Ipv6ExtensionHeaderParsingError::ErroneousHeaderField {
                    pointer: (context.bytes_parsed as u32) + 2,
                    must_send_icmp: true,
                    header_len: context.bytes_parsed,
                });
            }
        }

        // Parse Routing Type 0 specific data

        // Each address takes up 16 bytes.
        // As per RFC 8200 section 4.4, Hdr Ext Len is the length of this extension
        // header in  8-octet units, not including the first 8 octets.
        //
        // Given this information, we know that to find the number of addresses,
        // we can simply divide the HdrExtLen by 2 to get the number of addresses.

        // First check to make sure we have enough data. Note, routing type 0 headers
        // have a 4 byte reserved field immediately after the first 4 bytes (before
        // the first address) so we account for that when we do our check as well.
        let expected_len = (hdr_ext_len as usize) * 8 + 4;

        if expected_len > data.len() {
            return Err(Ipv6ExtensionHeaderParsingError::BufferExhausted);
        }

        // If HdrExtLen is an odd number, send an ICMP Parameter Problem, code 0,
        // pointing to the HdrExtLen field.
        if (hdr_ext_len & 0x1) == 0x1 {
            return Err(Ipv6ExtensionHeaderParsingError::ErroneousHeaderField {
                pointer: (context.bytes_parsed as u32) + 1,
                must_send_icmp: true,
                header_len: context.bytes_parsed,
            });
        }

        // Discard 4 reserved bytes, but because of our earlier check to make sure
        // `data` contains at least `expected_len` bytes, we assert that we actually
        // get some bytes back.
        assert!(data.take_front(4).is_some());

        let num_addresses = (hdr_ext_len as usize) / 2;

        if (segments_left as usize) > num_addresses {
            // Segments Left cannot be greater than the number of addresses.
            // If it is, we need to send an ICMP Parameter Problem, Code 0,
            // pointing to the Segments Left field.
            return Err(Ipv6ExtensionHeaderParsingError::ErroneousHeaderField {
                pointer: (context.bytes_parsed as u32) + 3,
                must_send_icmp: true,
                header_len: context.bytes_parsed,
            });
        }

        // Each address is an IPv6 Address which requires 16 bytes.
        // The below call to `take_front` is guaranteed to succeed because
        // we have already cheked to make sure we have enough bytes
        // in `data` to handle the total number of addresses, `num_addresses`.
        let addresses = data.take_front(num_addresses * 16).unwrap();

        // This is also guranteed to succeed because of the same comments as above.
        let addresses = LimitedRecords::parse_with_context(addresses, num_addresses).unwrap();

        // Update context
        context.next_header = next_header;
        context.headers_parsed += 1;
        context.bytes_parsed += 4 + expected_len;

        return Ok(Some(Some(Ipv6ExtensionHeader {
            next_header,
            data: Ipv6ExtensionHeaderData::Routing {
                routing_data: RoutingData {
                    bytes: routing_data,
                    type_specific_data: RoutingTypeSpecificData::RoutingType0 { addresses },
                },
            },
        })));
    }

    /// Parse Fragment Extension Header.
    fn parse_fragment<'a, BV: BufferView<&'a [u8]>>(
        data: &mut BV,
        context: &mut Ipv6ExtensionHeaderParsingContext,
    ) -> Result<Option<Option<Ipv6ExtensionHeader<'a>>>, Ipv6ExtensionHeaderParsingError> {
        // Fragment Extension Header requires exactly 8 bytes so make sure
        // `data` has at least 8 bytes left. If `data` has at least 8 bytes left,
        // we are guaranteed that all `take_front` calls done by this
        // method will succeed since we will never attempt to call `take_front`
        // with more than 8 bytes total.
        if data.len() < 8 {
            return Err(Ipv6ExtensionHeaderParsingError::BufferExhausted);
        }

        // For Fragment headers, we do not actually have a HdrExtLen field. Instead,
        // the second byte in the header (where HdrExtLen would normally exist), is
        // a reserved field, so we can simply ignore it for now.
        let (next_header, _) = Self::get_next_hdr_and_len(data, context)?;

        // Update context
        context.next_header = next_header;
        context.headers_parsed += 1;
        context.bytes_parsed += 8;

        return Ok(Some(Some(Ipv6ExtensionHeader {
            next_header,
            data: Ipv6ExtensionHeaderData::Fragment {
                fragment_data: FragmentData { bytes: data.take_front(6).unwrap() },
            },
        })));
    }

    /// Parse Destination Options Extension Header.
    fn parse_destination_options<'a, BV: BufferView<&'a [u8]>>(
        data: &mut BV,
        context: &mut Ipv6ExtensionHeaderParsingContext,
    ) -> Result<Option<Option<Ipv6ExtensionHeader<'a>>>, Ipv6ExtensionHeaderParsingError> {
        let (next_header, hdr_ext_len) = Self::get_next_hdr_and_len(data, context)?;

        // As per RFC 8200 section 4.6, Hdr Ext Len is the length of this extension
        // header in  8-octet units, not including the first 8 octets (where 2 of
        // them are the Next Header and the Hdr Ext Len fields).
        let expected_len = (hdr_ext_len as usize) * 8 + 6;

        let options = data
            .take_front(expected_len)
            .ok_or_else(|| Ipv6ExtensionHeaderParsingError::BufferExhausted)?;

        let options_context = ExtensionHeaderOptionContext::new();
        let options = Records::parse_with_context(options, options_context).map_err(|e| {
            // We know the below `try_from` call will not result in a `None` value because
            // the maximum size of an IPv6 packet's payload (extension headers + body) is
            // `std::u32::MAX`. This maximum size is only possible when using IPv6
            // jumbograms as defined by RFC 2675, which uses a 32 bit field for the payload
            // length. If we receive such a hypothetical packet with the maximum possible
            // payload length which only contains extension headers, we know that the offset
            // of any location within the payload must fit within an `u32`. If the packet is
            // a normal IPv6 packet (not a jumbogram), the maximum size of the payload is
            // `std::u16::MAX` (as the normal payload length field is only 16 bits), which
            // is significantly less than the maximum possible size of a jumbogram.
            ext_hdr_opt_err_to_ext_hdr_err(
                u32::try_from(context.bytes_parsed + 2).unwrap(),
                context.bytes_parsed,
                e,
            )
        })?;

        // Update context
        context.next_header = next_header;
        context.headers_parsed += 1;
        context.bytes_parsed += 2 + expected_len;

        return Ok(Some(Some(Ipv6ExtensionHeader {
            next_header,
            data: Ipv6ExtensionHeaderData::DestinationOptions { options },
        })));
    }
}

impl RecordsImplLayout for Ipv6ExtensionHeaderImpl {
    type Context = Ipv6ExtensionHeaderParsingContext;
    type Error = Ipv6ExtensionHeaderParsingError;
}

impl<'a> RecordsImpl<'a> for Ipv6ExtensionHeaderImpl {
    type Record = Ipv6ExtensionHeader<'a>;

    fn parse_with_context<BV: BufferView<&'a [u8]>>(
        data: &mut BV,
        context: &mut Self::Context,
    ) -> Result<Option<Option<Self::Record>>, Self::Error> {
        let expected_hdr = context.next_header;

        match Ipv6ExtHdrType::from(expected_hdr) {
            Ipv6ExtHdrType::HopByHopOptions => Self::parse_hop_by_hop_options(data, context),
            Ipv6ExtHdrType::Routing => Self::parse_routing(data, context),
            Ipv6ExtHdrType::Fragment => Self::parse_fragment(data, context),
            Ipv6ExtHdrType::DestinationOptions => Self::parse_destination_options(data, context),

            _ | Ipv6ExtHdrType::Other(_) => {
                if is_valid_next_header_upper_layer(expected_hdr) {
                    // Stop parsing extension headers when we find a Next Header value
                    // for a higher level protocol.
                    Ok(None)
                } else {
                    // Should never end up here because we guarantee that if we hit an
                    // invalid Next Header field while parsing extension headers, we will
                    // return an error when we see it right away. Since the only other time
                    // `context.next_header` can get an invalid value assigned is when we parse
                    // the fixed IPv6 header, but we check if the next header is valid before
                    // parsing extension headers.

                    unreachable!(
                        "Should never try parsing an extension header with an unrecognized type"
                    );
                }
            }
        }
    }
}

//
// Hop-By-Hop Options
//

type HopByHopOption<'a> = ExtensionHeaderOption<HopByHopOptionData<'a>>;
type HopByHopOptionsImpl = ExtensionHeaderOptionImpl<HopByHopOptionDataImpl>;

/// HopByHop Options Extension header data.
#[derive(Debug)]
pub(crate) enum HopByHopOptionData<'a> {
    Unrecognized { kind: u8, len: u8, data: &'a [u8] },
}

/// Impl for Hop By Hop Options parsing.
#[derive(Debug)]
pub(crate) struct HopByHopOptionDataImpl;

impl ExtensionHeaderOptionDataImplLayout for HopByHopOptionDataImpl {
    type Context = ();
}

impl<'a> ExtensionHeaderOptionDataImpl<'a> for HopByHopOptionDataImpl {
    type OptionData = HopByHopOptionData<'a>;

    fn parse_option(
        kind: u8,
        data: &'a [u8],
        context: &mut Self::Context,
        allow_unrecognized: bool,
    ) -> Option<Self::OptionData> {
        if allow_unrecognized {
            Some(HopByHopOptionData::Unrecognized { kind, len: data.len() as u8, data })
        } else {
            None
        }
    }
}

//
// Routing
//

/// Routing Extension header data.
#[derive(Debug)]
pub(crate) struct RoutingData<'a> {
    bytes: &'a [u8],
    type_specific_data: RoutingTypeSpecificData<'a>,
}

impl<'a> RoutingData<'a> {
    pub(crate) fn routing_type(&self) -> u8 {
        debug_assert!(self.bytes.len() >= 2);
        self.bytes[0]
    }

    pub(crate) fn segments_left(&self) -> u8 {
        debug_assert!(self.bytes.len() >= 2);
        self.bytes[1]
    }

    pub(crate) fn type_specific_data(&self) -> &RoutingTypeSpecificData<'a> {
        &self.type_specific_data
    }
}

/// Routing Type specific data.
#[derive(Debug)]
pub(crate) enum RoutingTypeSpecificData<'a> {
    RoutingType0 { addresses: LimitedRecords<&'a [u8], RoutingType0Impl> },
}

#[derive(Debug)]
pub(crate) struct RoutingType0Impl;

impl LimitedRecordsImplLayout for RoutingType0Impl {
    type Error = ();
    const EXACT_LIMIT_ERROR: Option<()> = Some(());
}

impl<'a> LimitedRecordsImpl<'a> for RoutingType0Impl {
    type Record = LayoutVerified<&'a [u8], Ipv6Addr>;

    fn parse<BV: BufferView<&'a [u8]>>(
        data: &mut BV,
    ) -> Result<Option<Option<Self::Record>>, Self::Error> {
        match data.take_obj_front() {
            None => Err(()),
            Some(i) => Ok(Some(Some(i))),
        }
    }
}

//
// Fragment
//

/// Fragment Extension header data.
///
/// As per RFC 8200, section 4.5 the fragment header is structured as:
/// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
/// |  Next Header  |   Reserved    |      Fragment Offset    |Res|M|
/// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
/// |                         Identification                        |
/// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
///
/// where Fragment Offset is 13 bits, Res is a reserved 2 bits and M
/// is a 1 bit flag. Identification is a 32bit value.
#[derive(Debug)]
pub(crate) struct FragmentData<'a> {
    bytes: &'a [u8],
}

impl<'a> FragmentData<'a> {
    pub(crate) fn fragment_offset(&self) -> u16 {
        debug_assert!(self.bytes.len() == 6);
        (((self.bytes[0] as u16) << 5) | ((self.bytes[1] as u16) >> 3))
    }

    pub(crate) fn m_flag(&self) -> bool {
        debug_assert!(self.bytes.len() == 6);
        ((self.bytes[1] & 0x1) == 0x01)
    }

    pub(crate) fn identification(&self) -> u32 {
        debug_assert!(self.bytes.len() == 6);
        NetworkEndian::read_u32(&self.bytes[2..6])
    }
}

//
// Destination Options
//

type DestinationOption<'a> = ExtensionHeaderOption<DestinationOptionData<'a>>;
type DestinationOptionsImpl = ExtensionHeaderOptionImpl<DestinationOptionDataImpl>;

/// Destination Options extension header data.
#[derive(Debug)]
pub(crate) enum DestinationOptionData<'a> {
    Unrecognized { kind: u8, len: u8, data: &'a [u8] },
}

/// Impl for Destination Options parsing.
#[derive(Debug)]
pub(crate) struct DestinationOptionDataImpl;

impl ExtensionHeaderOptionDataImplLayout for DestinationOptionDataImpl {
    type Context = ();
}

impl<'a> ExtensionHeaderOptionDataImpl<'a> for DestinationOptionDataImpl {
    type OptionData = DestinationOptionData<'a>;

    fn parse_option(
        kind: u8,
        data: &'a [u8],
        context: &mut Self::Context,
        allow_unrecognized: bool,
    ) -> Option<Self::OptionData> {
        if allow_unrecognized {
            Some(DestinationOptionData::Unrecognized { kind, len: data.len() as u8, data })
        } else {
            None
        }
    }
}

//
// Generic Extension Header who's data are options.
//

/// Context that gets passed around when parsing IPv6 Extension Header options.
#[derive(Debug, Clone)]
pub(crate) struct ExtensionHeaderOptionContext<C: Sized + Clone> {
    // Counter for number of options parsed.
    options_parsed: usize,

    // Byte count of succesfully parsed options.
    bytes_parsed: usize,

    // Extension header specific context data.
    specific_context: C,
}

impl<C: Sized + Clone + Default> ExtensionHeaderOptionContext<C> {
    fn new() -> Self {
        ExtensionHeaderOptionContext {
            options_parsed: 0,
            bytes_parsed: 0,
            specific_context: C::default(),
        }
    }
}

impl<C: Sized + Clone> RecordsContext for ExtensionHeaderOptionContext<C> {}

/// Basic associated types required by `ExtensionHeaderOptionDataImpl`.
pub(crate) trait ExtensionHeaderOptionDataImplLayout {
    type Context: RecordsContext;
}

/// An implementation of an extension header specific option data parser.
pub(crate) trait ExtensionHeaderOptionDataImpl<'a>:
    ExtensionHeaderOptionDataImplLayout
{
    /// Extension header specific option data.
    ///
    /// Note, `OptionData` does not need to hold general option data as defined by
    /// RFC 8200 section 4.2. It should only hold extension header specific option
    /// data.
    type OptionData: Sized;

    /// Parse an option of a given `kind` from `data`.
    ///
    /// When `kind` is recognized returns `Ok(o)` where `o` is a successfully parsed
    /// option. When `kind` is not recognized, returnd `None` if `allow_unrecognized`
    /// is `false`. If `kind` is not recognized but `allow_unrecognized` is `true`,
    /// returns an `Ok(o)` where `o` holds option data without actually parsing it
    /// (i.e. an unrecognized type that simply keeps track of the `kind` and `data`
    /// that was passed to `parse_option`).
    fn parse_option(
        kind: u8,
        data: &'a [u8],
        context: &mut Self::Context,
        allow_unrecognized: bool,
    ) -> Option<Self::OptionData>;
}

/// Generic implementation of extension header options parsing.
///
/// `ExtensionHeaderOptionImpl` handles the common implementation details
/// of extension header options and lets `O` (which implements
/// `ExtensionHeaderOptionDataImpl`) handle the extension header specific
/// option parsing.
#[derive(Debug)]
pub(crate) struct ExtensionHeaderOptionImpl<O>(PhantomData<O>);

impl<O> ExtensionHeaderOptionImpl<O> {
    const PAD1: u8 = 0;
    const PADN: u8 = 1;
}

impl<O> RecordsImplLayout for ExtensionHeaderOptionImpl<O>
where
    O: ExtensionHeaderOptionDataImplLayout,
{
    type Error = ExtensionHeaderOptionParsingError;
    type Context = ExtensionHeaderOptionContext<O::Context>;
}

impl<'a, O> RecordsImpl<'a> for ExtensionHeaderOptionImpl<O>
where
    O: ExtensionHeaderOptionDataImpl<'a>,
{
    type Record = ExtensionHeaderOption<O::OptionData>;

    fn parse_with_context<BV: BufferView<&'a [u8]>>(
        data: &mut BV,
        context: &mut Self::Context,
    ) -> Result<Option<Option<Self::Record>>, Self::Error> {
        // If we have no more bytes left, we are done.
        let kind = match data.take_front(1).map(|x| x[0]) {
            None => return Ok(None),
            Some(k) => k,
        };

        // Will never get an error because we only use the 2 least significant bits which
        // can only have a max value of 3 and all values in [0, 3] are valid values of
        // `ExtensionHeaderOptionAction`.
        let action =
            ExtensionHeaderOptionAction::try_from((kind >> 6) & 0x3).expect("Unexpected error");
        let mutable = ((kind >> 5) & 0x1) == 0x1;
        let kind = kind & 0x1F;

        // If our kind is a PAD1, consider it a NOP.
        if kind == Self::PAD1 {
            // Update context.
            context.options_parsed += 1;
            context.bytes_parsed += 1;

            return Ok(Some(None));
        }

        let len = data
            .take_front(1)
            .map(|x| x[0])
            .ok_or_else(|| ExtensionHeaderOptionParsingError::BufferExhausted)?;

        let data = data
            .take_front(len as usize)
            .ok_or_else(|| ExtensionHeaderOptionParsingError::BufferExhausted)?;

        // If our kind is a PADN, consider it a NOP as well.
        if kind == Self::PADN {
            // Update context.
            context.options_parsed += 1;
            context.bytes_parsed += 2 + (len as usize);

            return Ok(Some(None));
        }

        // Parse the actual option data.
        match O::parse_option(
            kind,
            data,
            &mut context.specific_context,
            action == ExtensionHeaderOptionAction::SkipAndContinue,
        ) {
            Some(o) => {
                // Update context.
                context.options_parsed += 1;
                context.bytes_parsed += 2 + (len as usize);

                Ok(Some(Some(ExtensionHeaderOption { action, mutable, data: o })))
            }
            None => {
                // Unrecognized option type.
                match action {
                    // `O::parse_option` should never return `None` when the action is
                    // `ExtensionHeaderOptionAction::SkipAndContinue` because we expect
                    // `O::parse_option` to return something that holds the option data
                    // without actually parsing it since we pass `true` for its
                    // `allow_unrecognized` parameter.
                    ExtensionHeaderOptionAction::SkipAndContinue => unreachable!(
                        "Should never end up here since action was set to skip and continue"
                    ),
                    // We know the below `try_from` call will not result in a `None` value because
                    // the maximum size of an IPv6 packet's payload (extension headers + body) is
                    // `std::u32::MAX`. This maximum size is only possible when using IPv6
                    // jumbograms as defined by RFC 2675, which uses a 32 bit field for the payload
                    // length. If we receive such a hypothetical packet with the maximum possible
                    // payload length which only contains extension headers, we know that the offset
                    // of any location within the payload must fit within an `u32`. If the packet is
                    // a normal IPv6 packet (not a jumbogram), the maximum size of the payload is
                    // `std::u16::MAX` (as the normal payload length field is only 16 bits), which
                    // is significantly less than the maximum possible size of a jumbogram.
                    _ => Err(ExtensionHeaderOptionParsingError::UnrecognizedOption {
                        pointer: u32::try_from(context.bytes_parsed).unwrap(),
                        action,
                    }),
                }
            }
        }
    }
}

/// Possible errors when parsing extension header options.
#[derive(Debug, PartialEq, Eq)]
pub(crate) enum ExtensionHeaderOptionParsingError {
    UnrecognizedOption { pointer: u32, action: ExtensionHeaderOptionAction },
    BufferExhausted,
}

/// Action to take when an unrecognized option type is encountered.
///
/// `ExtensionHeaderOptionAction` is an action that MUST be taken (according
/// to RFC 8200 section 4.2) when an an IPv6 processing node does not
/// recognize an option's type.
#[derive(Debug, PartialEq, Eq)]
pub(crate) enum ExtensionHeaderOptionAction {
    /// Skip over the option and continue processing the header.
    /// value = 0.
    SkipAndContinue,

    /// Just discard the packet.
    /// value = 1.
    DiscardPacket,

    /// Discard the packet and, regardless of whether or not the packet's
    /// destination address was a multicast address, send an ICMP parameter
    /// problem, code 2 (unrecognized option), message to the packet's source
    /// address, pointing to the unrecognized type.
    /// value = 2.
    DiscardPacketSendICMP,

    /// Discard the packet and, and only if the packet's destination address
    /// was not a multicast address, send an ICMP parameter problem, code 2
    /// (unrecognized option), message to the packet's source address, pointing
    /// to the unrecognized type.
    /// value = 3.
    DiscardPacketSendICMPNoMulticast,
}

impl TryFrom<u8> for ExtensionHeaderOptionAction {
    type Error = ();

    fn try_from(value: u8) -> Result<Self, ()> {
        match value {
            0 => Ok(ExtensionHeaderOptionAction::SkipAndContinue),
            1 => Ok(ExtensionHeaderOptionAction::DiscardPacket),
            2 => Ok(ExtensionHeaderOptionAction::DiscardPacketSendICMP),
            3 => Ok(ExtensionHeaderOptionAction::DiscardPacketSendICMPNoMulticast),
            _ => Err(()),
        }
    }
}

impl Into<u8> for ExtensionHeaderOptionAction {
    fn into(self) -> u8 {
        match self {
            ExtensionHeaderOptionAction::SkipAndContinue => 0,
            ExtensionHeaderOptionAction::DiscardPacket => 1,
            ExtensionHeaderOptionAction::DiscardPacketSendICMP => 2,
            ExtensionHeaderOptionAction::DiscardPacketSendICMPNoMulticast => 3,
        }
    }
}

/// Extension header option.
///
/// Generic Extension header option type that has extension header specific
/// option data (`data`) defined by an `O`. The common option format is defined in
/// section 4.2 of RFC 8200, outlining actions and mutability for option types.
pub(crate) struct ExtensionHeaderOption<O> {
    /// Action to take if the option type is unrecognized.
    pub(crate) action: ExtensionHeaderOptionAction,

    /// Whether or not the option data of the option can change en route to the
    /// packet's final destination. When an Authentication header is present in
    /// the packet, the option data must be treated as 0s when computing or
    /// verifying the packet's authenticating value when the option data can change
    /// en route.
    pub(crate) mutable: bool,

    /// Option data associated with a specific extension header.
    pub(crate) data: O,
}

//
// Helper functions
//

/// Make sure a Next Header is valid.
///
/// Check if the provided `next_header` is a valid Next Header value. Note,
/// we are intentionally not allowing HopByHopOptions after the first Next
/// Header as per section 4.1 of RFC 8200 which restricts the HopByHop extension
/// header to only appear as the very first extension header. `is_valid_next_header`.
/// If a caller specifies `for_fixed_header` as true, then it is assumed `next_header` is
/// the Next Header value in the fixed header, where a HopbyHopOptions extension
/// header number is allowed.
pub(super) fn is_valid_next_header(next_header: u8, for_fixed_header: bool) -> bool {
    // Make sure the Next Header in the fixed header is a valid extension
    // header or a valid upper layer protocol.

    match Ipv6ExtHdrType::from(next_header) {
        // HopByHop Options Extension header as a next header value
        // is only valid if it is in the fixed header.
        Ipv6ExtHdrType::HopByHopOptions => for_fixed_header,

        // Not an IPv6 Extension header number, so make sure it is
        // a valid upper layer protocol.
        Ipv6ExtHdrType::Other(next_header) => is_valid_next_header_upper_layer(next_header),

        // All valid Extension Header numbers
        _ => true,
    }
}

/// Make sure a Next Header is a valid upper layer protocol.
///
/// Make sure a Next Header is a valid upper layer protocol in an IPv6 packet. Note,
/// we intentionally are not allowing ICMP(v4) since we are working on IPv6 packets.
pub(super) fn is_valid_next_header_upper_layer(next_header: u8) -> bool {
    match IpProto::from(next_header) {
        IpProto::Igmp
        | IpProto::Tcp
        | IpProto::Tcp
        | IpProto::Udp
        | IpProto::Icmpv6
        | IpProto::NoNextHeader => true,
        _ => false,
    }
}

/// Convert an `ExtensionHeaderOptionParsingError` to an
/// `Ipv6ExtensionHeaderParsingError`.
///
/// `offset` is the offset of the start of the options containing the error, `err`,
/// from the end of the fixed header in an IPv6 packet. `header_len` is the
/// length of the IPv6 header (including extension headers) that we know about up
/// to the point of the error, `err`. Note, any data in a packet after the first
/// `header_len` bytes is not parsed, so its context is unknown.
fn ext_hdr_opt_err_to_ext_hdr_err(
    offset: u32,
    header_len: usize,
    err: ExtensionHeaderOptionParsingError,
) -> Ipv6ExtensionHeaderParsingError {
    match err {
        ExtensionHeaderOptionParsingError::UnrecognizedOption { pointer, action } => {
            Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
                pointer: offset + pointer,
                must_send_icmp: true,
                header_len,
                action,
            }
        }
        ExtensionHeaderOptionParsingError::BufferExhausted => {
            Ipv6ExtensionHeaderParsingError::BufferExhausted
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::wire::util::records::Records;

    #[test]
    fn test_is_valid_next_header_upper_layer() {
        // Make sure upper layer protocols like tcp is valid
        assert!(is_valid_next_header_upper_layer(IpProto::Tcp.into()));
        assert!(is_valid_next_header_upper_layer(IpProto::Tcp.into()));

        // Make sure upper layer protocol ICMP(v4) is not valid
        assert!(!is_valid_next_header_upper_layer(IpProto::Icmp.into()));
        assert!(!is_valid_next_header_upper_layer(IpProto::Icmp.into()));

        // Make sure any other value is not valid.
        // Note, if 255 becomes a valid value, we should fix this test
        assert!(!is_valid_next_header(255, true));
        assert!(!is_valid_next_header(255, false));
    }

    #[test]
    fn test_is_valid_next_header() {
        // Make sure HopByHop Options is only valid if it is in the first Next Header
        // (In the fixed header).
        assert!(is_valid_next_header(Ipv6ExtHdrType::HopByHopOptions.into(), true));
        assert!(!is_valid_next_header(Ipv6ExtHdrType::HopByHopOptions.into(), false));

        // Make sure other extension headers (like routing) can be in any
        // Next Header
        assert!(is_valid_next_header(Ipv6ExtHdrType::Routing.into(), true));
        assert!(is_valid_next_header(Ipv6ExtHdrType::Routing.into(), false));

        // Make sure upper layer protocols like tcp can be in any Next Header
        assert!(is_valid_next_header(IpProto::Tcp.into(), true));
        assert!(is_valid_next_header(IpProto::Tcp.into(), false));

        // Make sure upper layer protocol ICMP(v4) cannot be in any Next Header
        assert!(!is_valid_next_header(IpProto::Icmp.into(), true));
        assert!(!is_valid_next_header(IpProto::Icmp.into(), false));

        // Make sure any other value is not valid.
        // Note, if 255 becomes a valid value, we should fix this test
        assert!(!is_valid_next_header(255, true));
        assert!(!is_valid_next_header(255, false));
    }

    #[test]
    fn test_hop_by_hop_options() {
        // Test parsing of Pad1 (marked as NOP)
        let buffer = [0; 10];
        let mut context = ExtensionHeaderOptionContext::new();
        let options =
            Records::<_, HopByHopOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .unwrap();
        assert_eq!(options.iter().count(), 0);
        assert_eq!(context.bytes_parsed, 10);
        assert_eq!(context.options_parsed, 10);

        // Test parsing of Pad1 w/ PadN (treated as NOP)
        #[rustfmt::skip]
        let buffer = [
            0,                            // Pad1
            1, 0,                         // Pad2
            1, 8, 0, 0, 0, 0, 0, 0, 0, 0, // Pad10
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        let options =
            Records::<_, HopByHopOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .unwrap();
        assert_eq!(options.iter().count(), 0);
        assert_eq!(context.bytes_parsed, 13);
        assert_eq!(context.options_parsed, 3);

        // Test parsing with an unknown option type but its action is
        // skip/continue
        #[rustfmt::skip]
        let buffer = [
            0,                            // Pad1
            63, 1, 0,                     // Unrecognized Option Type but can skip/continue
            1,  6, 0, 0, 0, 0, 0, 0,      // Pad8
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        let options =
            Records::<_, HopByHopOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .unwrap();
        let options: Vec<HopByHopOption> = options.iter().collect();
        assert_eq!(options.len(), 1);
        assert_eq!(options[0].action, ExtensionHeaderOptionAction::SkipAndContinue);
        assert_eq!(context.bytes_parsed, 12);
        assert_eq!(context.options_parsed, 3);
    }

    #[test]
    fn test_hop_by_hop_options_err() {
        // Test parsing but missing last 2 bytes
        #[rustfmt::skip]
        let buffer = [
            0,                            // Pad1
            1, 0,                         // Pad2
            1, 8, 0, 0, 0, 0, 0, 0,       // Pad10 (but missing 2 bytes)
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        Records::<_, HopByHopOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
            .expect_err("Parsed successfully when we were short 2 by bytes");
        assert_eq!(context.bytes_parsed, 3);
        assert_eq!(context.options_parsed, 2);

        // Test parsing with unknown option type but action set to discard
        #[rustfmt::skip]
        let buffer = [
            1,   1, 0,                    // Pad3
            127, 0,                       // Unrecognized Option Type w/ action to discard
            1,   6, 0, 0, 0, 0, 0, 0,     // Pad8
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        assert_eq!(
            Records::<_, HopByHopOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .expect_err("Parsed successfully when we had an unrecognized option type"),
            ExtensionHeaderOptionParsingError::UnrecognizedOption {
                pointer: 3,
                action: ExtensionHeaderOptionAction::DiscardPacket,
            }
        );
        assert_eq!(context.bytes_parsed, 3);
        assert_eq!(context.options_parsed, 1);

        // Test parsing with unknown option type but action set to discard and
        // send ICMP.
        #[rustfmt::skip]
        let buffer = [
            1,   1, 0,                    // Pad3
            191, 0,                       // Unrecognized Option Type w/ action to discard
                                          // & send icmp
            1,   6, 0, 0, 0, 0, 0, 0,     // Pad8
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        assert_eq!(
            Records::<_, HopByHopOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .expect_err("Parsed successfully when we had an unrecognized option type"),
            ExtensionHeaderOptionParsingError::UnrecognizedOption {
                pointer: 3,
                action: ExtensionHeaderOptionAction::DiscardPacketSendICMP,
            }
        );
        assert_eq!(context.bytes_parsed, 3);
        assert_eq!(context.options_parsed, 1);

        // Test parsing with unknown option type but action set to discard and
        // send ICMP if not sending to a multicast address
        #[rustfmt::skip]
        let buffer = [
            1,   1, 0,                    // Pad3
            255, 0,                       // Unrecognized Option Type w/ action to discard
                                          // & send icmp if no multicast
            1,   6, 0, 0, 0, 0, 0, 0,     // Pad8
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        assert_eq!(
            Records::<_, HopByHopOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .expect_err("Parsed successfully when we had an unrecognized option type"),
            ExtensionHeaderOptionParsingError::UnrecognizedOption {
                pointer: 3,
                action: ExtensionHeaderOptionAction::DiscardPacketSendICMPNoMulticast,
            }
        );
        assert_eq!(context.bytes_parsed, 3);
        assert_eq!(context.options_parsed, 1);
    }

    #[test]
    fn test_routing_type0_limited_records() {
        // Test empty buffer
        let buffer = [0; 0];
        let addresses =
            LimitedRecords::<_, RoutingType0Impl>::parse_with_context(&buffer[..], 0).unwrap();
        assert_eq!(addresses.iter().count(), 0);

        // Test single address
        let buffer = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];
        let addresses =
            LimitedRecords::<_, RoutingType0Impl>::parse_with_context(&buffer[..], 1).unwrap();
        let addresses: Vec<LayoutVerified<_, Ipv6Addr>> = addresses.iter().collect();
        assert_eq!(addresses.len(), 1);
        assert_eq!(addresses[0].bytes(), [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,]);

        // Test multiple address
        #[rustfmt::skip]
        let buffer = [
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
            32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
        ];
        let addresses =
            LimitedRecords::<_, RoutingType0Impl>::parse_with_context(&buffer[..], 3).unwrap();
        let addresses: Vec<LayoutVerified<_, Ipv6Addr>> = addresses.iter().collect();
        assert_eq!(addresses.len(), 3);
        assert_eq!(addresses[0].bytes(), [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,]);
        assert_eq!(
            addresses[1].bytes(),
            [16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,]
        );
        assert_eq!(
            addresses[2].bytes(),
            [32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,]
        );
    }

    #[test]
    fn test_routing_type0_limited_records_err() {
        // Test single address with not enough bytes
        let buffer = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13];
        LimitedRecords::<_, RoutingType0Impl>::parse_with_context(&buffer[..], 1)
            .expect_err("Parsed successfully when we were misisng 2 bytes");

        // Test multiple addresses but not enough bytes for last one
        #[rustfmt::skip]
        let buffer = [
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
            32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
        ];
        LimitedRecords::<_, RoutingType0Impl>::parse_with_context(&buffer[..], 3)
            .expect_err("Parsed successfully when we were misisng 2 bytes");

        // Test multiple addresses but limit is more than what we expected
        #[rustfmt::skip]
        let buffer = [
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
            32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47
        ];
        LimitedRecords::<_, RoutingType0Impl>::parse_with_context(&buffer[..], 4)
            .expect_err("Parsed 3 addresses succesfully when exact limit was set to 4");
    }

    #[test]
    fn test_destination_options() {
        // Test parsing of Pad1 (marked as NOP)
        let buffer = [0; 10];
        let mut context = ExtensionHeaderOptionContext::new();
        let options =
            Records::<_, DestinationOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .unwrap();
        assert_eq!(options.iter().count(), 0);
        assert_eq!(context.bytes_parsed, 10);
        assert_eq!(context.options_parsed, 10);

        // Test parsing of Pad1 w/ PadN (treated as NOP)
        #[rustfmt::skip]
        let buffer = [
            0,                            // Pad1
            1, 0,                         // Pad2
            1, 8, 0, 0, 0, 0, 0, 0, 0, 0, // Pad10
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        let options =
            Records::<_, DestinationOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .unwrap();
        assert_eq!(options.iter().count(), 0);
        assert_eq!(context.bytes_parsed, 13);
        assert_eq!(context.options_parsed, 3);

        // Test parsing with an unknown option type but its action is
        // skip/continue
        #[rustfmt::skip]
        let buffer = [
            0,                            // Pad1
            63, 1, 0,                     // Unrecognized Option Type but can skip/continue
            1,  6, 0, 0, 0, 0, 0, 0,      // Pad8
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        let options =
            Records::<_, DestinationOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .unwrap();
        let options: Vec<DestinationOption> = options.iter().collect();
        assert_eq!(options.len(), 1);
        assert_eq!(options[0].action, ExtensionHeaderOptionAction::SkipAndContinue);
        assert_eq!(context.bytes_parsed, 12);
        assert_eq!(context.options_parsed, 3);
    }

    #[test]
    fn test_destination_options_err() {
        // Test parsing but missing last 2 bytes
        #[rustfmt::skip]
        let buffer = [
            0,                            // Pad1
            1, 0,                         // Pad2
            1, 8, 0, 0, 0, 0, 0, 0,       // Pad10 (but missing 2 bytes)
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        Records::<_, DestinationOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
            .expect_err("Parsed successfully when we were short 2 by bytes");
        assert_eq!(context.bytes_parsed, 3);
        assert_eq!(context.options_parsed, 2);

        // Test parsing with unknown option type but action set to discard
        #[rustfmt::skip]
        let buffer = [
            1,   1, 0,                    // Pad3
            127, 0,                       // Unrecognized Option Type w/ action to discard
            1,   6, 0, 0, 0, 0, 0, 0,     // Pad8
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        assert_eq!(
            Records::<_, DestinationOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .expect_err("Parsed successfully when we had an unrecognized option type"),
            ExtensionHeaderOptionParsingError::UnrecognizedOption {
                pointer: 3,
                action: ExtensionHeaderOptionAction::DiscardPacket,
            }
        );
        assert_eq!(context.bytes_parsed, 3);
        assert_eq!(context.options_parsed, 1);

        // Test parsing with unknown option type but action set to discard and
        // send ICMP.
        #[rustfmt::skip]
        let buffer = [
            1,   1, 0,                    // Pad3
            191, 0,                       // Unrecognized Option Type w/ action to discard
                                          // & send icmp
            1,   6, 0, 0, 0, 0, 0, 0,     // Pad8
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        assert_eq!(
            Records::<_, DestinationOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .expect_err("Parsed successfully when we had an unrecognized option type"),
            ExtensionHeaderOptionParsingError::UnrecognizedOption {
                pointer: 3,
                action: ExtensionHeaderOptionAction::DiscardPacketSendICMP,
            }
        );
        assert_eq!(context.bytes_parsed, 3);
        assert_eq!(context.options_parsed, 1);

        // Test parsing with unknown option type but action set to discard and
        // send ICMP if not sending to a multicast address
        #[rustfmt::skip]
        let buffer = [
            1,   1, 0,                    // Pad3
            255, 0,                       // Unrecognized Option Type w/ action to discard
                                          // & send icmp if no multicast
            1,   6, 0, 0, 0, 0, 0, 0,     // Pad8
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        assert_eq!(
            Records::<_, DestinationOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .expect_err("Parsed successfully when we had an unrecognized option type"),
            ExtensionHeaderOptionParsingError::UnrecognizedOption {
                pointer: 3,
                action: ExtensionHeaderOptionAction::DiscardPacketSendICMPNoMulticast,
            }
        );
        assert_eq!(context.bytes_parsed, 3);
        assert_eq!(context.options_parsed, 1);
    }

    #[test]
    fn test_hop_by_hop_options_ext_hdr() {
        // Test parsing of just a single Hop By Hop Extension Header.
        // The hop by hop options will only be pad options.
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(),     // Next Header
            1,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            1,  4, 0, 0, 0, 0,       // Pad6
            63, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action set to skip/continue
        ];
        let ext_hdrs =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .unwrap();
        let ext_hdrs: Vec<Ipv6ExtensionHeader> = ext_hdrs.iter().collect();
        assert_eq!(ext_hdrs.len(), 1);
        assert_eq!(ext_hdrs[0].next_header, IpProto::Tcp.into());
        if let Ipv6ExtensionHeaderData::HopByHopOptions { options } = ext_hdrs[0].data() {
            // Everything should have been a NOP/ignore except for the unrecognized type
            let options: Vec<HopByHopOption> = options.iter().collect();
            assert_eq!(options.len(), 1);
            assert_eq!(options[0].action, ExtensionHeaderOptionAction::SkipAndContinue);
        } else {
            panic!("Should have matched HopByHopOptions {:?}", ext_hdrs[0].data());
        }
    }

    #[test]
    fn test_hop_by_hop_options_ext_hdr_err() {
        // Test parsing of just a single Hop By Hop Extension Header with errors.

        // Test with invalid Next Header
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
        #[rustfmt::skip]
        let buffer = [
            255,                  // Next Header (Invalid)
            0,                    // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            1, 4, 0, 0, 0, 0,     // Pad6
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed succesfully when the next header was invalid");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedNextHeader {
            pointer,
            must_send_icmp,
            header_len,
        } = error
        {
            assert_eq!(pointer, 0);
            assert!(!must_send_icmp);
            assert_eq!(header_len, 0);
        } else {
            panic!("Should have matched with UnrecognizedNextHeader: {:?}", error);
        }

        // Test with invalid option type w/ action = discard.
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(),      // Next Header
            1,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            1,   4, 0, 0, 0, 0,       // Pad6
            127, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action = discard
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed succesfully with an unrecognized option type");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
            pointer,
            must_send_icmp,
            header_len,
            action,
        } = error
        {
            assert_eq!(pointer, 8);
            assert!(must_send_icmp);
            assert_eq!(header_len, 0);
            assert_eq!(action, ExtensionHeaderOptionAction::DiscardPacket);
        } else {
            panic!("Should have matched with UnrecognizedOption: {:?}", error);
        }

        // Test with invalid option type w/ action = discard & send icmp
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(),      // Next Header
            1,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            1,   4, 0, 0, 0, 0,       // Pad6
            191, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action = discard & send icmp
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed succesfully with an unrecognized option type");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
            pointer,
            must_send_icmp,
            header_len,
            action,
        } = error
        {
            assert_eq!(pointer, 8);
            assert!(must_send_icmp);
            assert_eq!(header_len, 0);
            assert_eq!(action, ExtensionHeaderOptionAction::DiscardPacketSendICMP);
        } else {
            panic!("Should have matched with UnrecognizedOption: {:?}", error);
        }

        // Test with invalid option type w/ action = discard & send icmp if not multicast
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(),      // Next Header
            1,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            1,   4, 0, 0, 0, 0,       // Pad6
            255, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action = discard & send icmp
                                      // if destination address is not a multicast
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed succesfully with an unrecognized option type");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
            pointer,
            must_send_icmp,
            header_len,
            action,
        } = error
        {
            assert_eq!(pointer, 8);
            assert!(must_send_icmp);
            assert_eq!(header_len, 0);
            assert_eq!(action, ExtensionHeaderOptionAction::DiscardPacketSendICMPNoMulticast);
        } else {
            panic!("Should have matched with UnrecognizedOption: {:?}", error);
        }
    }

    #[test]
    fn test_routing_ext_hdr() {
        // Test parsing of just a single Routing Extension Header.
        let context = Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::Routing.into());
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(), // Next Header
            4,                   // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                   // Routing Type
            1,                   // Segments Left
            0, 0, 0, 0,          // Reserved
            // Addresses for Routing Header w/ Type 0
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

        ];
        let ext_hdrs =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .unwrap();
        let ext_hdrs: Vec<Ipv6ExtensionHeader> = ext_hdrs.iter().collect();
        assert_eq!(ext_hdrs.len(), 1);
        assert_eq!(ext_hdrs[0].next_header, IpProto::Tcp.into());
        if let Ipv6ExtensionHeaderData::Routing { routing_data } = ext_hdrs[0].data() {
            assert_eq!(routing_data.routing_type(), 0);
            assert_eq!(routing_data.segments_left(), 1);

            let RoutingTypeSpecificData::RoutingType0 { addresses } =
                routing_data.type_specific_data();
            let addresses: Vec<LayoutVerified<_, Ipv6Addr>> = addresses.iter().collect();
            assert_eq!(addresses.len(), 2);
            assert_eq!(
                addresses[0].bytes(),
                [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,]
            );
            assert_eq!(
                addresses[1].bytes(),
                [16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,]
            );
        } else {
            panic!("Should have matched Routing: {:?}", ext_hdrs[0].data());
        }
    }

    #[test]
    fn test_routing_ext_hdr_err() {
        // Test parsing of just a single Routing Extension Header with errors.

        // Test Invalid Next Header
        let context = Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::Routing.into());
        #[rustfmt::skip]
        let buffer = [
            255,                 // Next Header (Invalid)
            4,                   // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                   // Routing Type
            1,                   // Segments Left
            0, 0, 0, 0,          // Reserved
            // Addresses for Routing Header w/ Type 0
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed succesfully when the next header was invalid");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedNextHeader {
            pointer,
            must_send_icmp,
            header_len,
        } = error
        {
            assert_eq!(pointer, 0);
            assert!(!must_send_icmp);
            assert_eq!(header_len, 0);
        } else {
            panic!("Should have matched with UnrecognizedNextHeader: {:?}", error);
        }

        // Test Unrecognized Routing Type
        let context = Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::Routing.into());
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(), // Next Header
            4,                   // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            255,                 // Routing Type (Invalid)
            1,                   // Segments Left
            0, 0, 0, 0,          // Reserved
            // Addresses for Routing Header w/ Type 0
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed succesfully with an unrecognized routing type");
        if let Ipv6ExtensionHeaderParsingError::ErroneousHeaderField {
            pointer,
            must_send_icmp,
            header_len,
        } = error
        {
            // Should point to the location of the routing type.
            assert_eq!(pointer, 2);
            assert!(must_send_icmp);
            assert_eq!(header_len, 0);
        } else {
            panic!("Should have matched with ErroneousHeaderField: {:?}", error);
        }

        // Test more Segments Left than addresses available
        let context = Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::Routing.into());
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(), // Next Header
            4,                   // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                   // Routing Type
            3,                   // Segments Left
            0, 0, 0, 0,          // Reserved
            // Addresses for Routing Header w/ Type 0
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err(
                "Parsed succesfully when segments left was greater than the number of addresses",
            );
        if let Ipv6ExtensionHeaderParsingError::ErroneousHeaderField {
            pointer,
            must_send_icmp,
            header_len,
        } = error
        {
            // Should point to the location of the routing type.
            assert_eq!(pointer, 3);
            assert!(must_send_icmp);
            assert_eq!(header_len, 0);
        } else {
            panic!("Should have matched with ErroneousHeaderField: {:?}", error);
        }
    }

    #[test]
    fn test_fragment_ext_hdr() {
        // Test parsing of just a single Fragment Extension Header.
        let context = Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::Fragment.into());
        let frag_offset_res_m_flag: u16 = (5063 << 3) | 1;
        let identification: u32 = 3266246449;
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(),                   // Next Header
            0,                                     // Reserved
            (frag_offset_res_m_flag >> 8) as u8,   // Fragment Offset MSB
            (frag_offset_res_m_flag & 0xFF) as u8, // Fragment Offset LS5bits w/ Res w/ M Flag
            // Identification
            (identification >> 24) as u8,
            ((identification >> 16) & 0xFF) as u8,
            ((identification >> 8) & 0xFF) as u8,
            (identification & 0xFF) as u8,
        ];
        let ext_hdrs =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .unwrap();
        let ext_hdrs: Vec<Ipv6ExtensionHeader> = ext_hdrs.iter().collect();
        assert_eq!(ext_hdrs.len(), 1);
        assert_eq!(ext_hdrs[0].next_header, IpProto::Tcp.into());

        if let Ipv6ExtensionHeaderData::Fragment { fragment_data } = ext_hdrs[0].data() {
            assert_eq!(fragment_data.fragment_offset(), 5063);
            assert_eq!(fragment_data.m_flag(), true);
            assert_eq!(fragment_data.identification(), 3266246449);
        } else {
            panic!("Should have matched Fragment: {:?}", ext_hdrs[0].data());
        }
    }

    #[test]
    fn test_fragment_ext_hdr_err() {
        // Test parsing of just a single Fragment Extension Header with errors.

        // Test invalid Next Header
        let context = Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::Fragment.into());
        let frag_offset_res_m_flag: u16 = (5063 << 3) | 1;
        let identification: u32 = 3266246449;
        #[rustfmt::skip]
        let buffer = [
            255,                                   // Next Header (Invalid)
            0,                                     // Reserved
            (frag_offset_res_m_flag >> 8) as u8,   // Fragment Offset MSB
            (frag_offset_res_m_flag & 0xFF) as u8, // Fragment Offset LS5bits w/ Res w/ M Flag
            // Identification
            (identification >> 24) as u8,
            ((identification >> 16) & 0xFF) as u8,
            ((identification >> 8) & 0xFF) as u8,
            (identification & 0xFF) as u8,
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed succesfully when the next header was invalid");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedNextHeader {
            pointer,
            must_send_icmp,
            header_len,
        } = error
        {
            assert_eq!(pointer, 0);
            assert!(!must_send_icmp);
            assert_eq!(header_len, 0);
        } else {
            panic!("Should have matched with UnrecognizedNextHeader: {:?}", error);
        }
    }

    #[test]
    fn test_no_next_header_ext_hdr() {
        // Test parsing of just a single NoNextHeader Extension Header.
        let context = Ipv6ExtensionHeaderParsingContext::new(IpProto::NoNextHeader.into());
        #[rustfmt::skip]
        let buffer = [0, 0, 0, 0,];
        let ext_hdrs =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .unwrap();
        assert_eq!(ext_hdrs.iter().count(), 0);
    }

    #[test]
    fn test_destination_options_ext_hdr() {
        // Test parsing of just a single Destination options Extension Header.
        // The destination options will only be pad options.
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::DestinationOptions.into());
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(),     // Next Header
            1,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            1, 4, 0, 0, 0, 0,        // Pad6
            63, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action set to skip/continue
        ];
        let ext_hdrs =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .unwrap();
        let ext_hdrs: Vec<Ipv6ExtensionHeader> = ext_hdrs.iter().collect();
        assert_eq!(ext_hdrs.len(), 1);
        assert_eq!(ext_hdrs[0].next_header, IpProto::Tcp.into());
        if let Ipv6ExtensionHeaderData::DestinationOptions { options } = ext_hdrs[0].data() {
            // Everything should have been a NOP/ignore except for the unrecognized type
            let options: Vec<DestinationOption> = options.iter().collect();
            assert_eq!(options.len(), 1);
            assert_eq!(options[0].action, ExtensionHeaderOptionAction::SkipAndContinue);
        } else {
            panic!("Should have matched DestinationOptions: {:?}", ext_hdrs[0].data());
        }
    }

    #[test]
    fn test_destination_options_ext_hdr_err() {
        // Test parsing of just a single Destination Options Extension Header with errors.
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::DestinationOptions.into());

        // Test with invalid Next Header
        #[rustfmt::skip]
        let buffer = [
            255,                  // Next Header (Invalid)
            0,                    // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            1, 4, 0, 0, 0, 0,     // Pad6
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed succesfully when the next header was invalid");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedNextHeader {
            pointer,
            must_send_icmp,
            header_len,
        } = error
        {
            assert_eq!(pointer, 0);
            assert!(!must_send_icmp);
            assert_eq!(header_len, 0);
        } else {
            panic!("Should have matched with UnrecognizedNextHeader: {:?}", error);
        }

        // Test with invalid option type w/ action = discard.
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::DestinationOptions.into());
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(),      // Next Header
            1,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            1,   4, 0, 0, 0, 0,       // Pad6
            127, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action = discard
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed succesfully with an unrecognized option type");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
            pointer,
            must_send_icmp,
            header_len,
            action,
        } = error
        {
            assert_eq!(pointer, 8);
            assert!(must_send_icmp);
            assert_eq!(header_len, 0);
            assert_eq!(action, ExtensionHeaderOptionAction::DiscardPacket);
        } else {
            panic!("Should have matched with UnrecognizedOption: {:?}", error);
        }

        // Test with invalid option type w/ action = discard & send icmp
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::DestinationOptions.into());
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(),      // Next Header
            1,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            1,   4, 0, 0, 0, 0,       // Pad6
            191, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action = discard & send icmp
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed succesfully with an unrecognized option type");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
            pointer,
            must_send_icmp,
            header_len,
            action,
        } = error
        {
            assert_eq!(pointer, 8);
            assert!(must_send_icmp);
            assert_eq!(header_len, 0);
            assert_eq!(action, ExtensionHeaderOptionAction::DiscardPacketSendICMP);
        } else {
            panic!("Should have matched with UnrecognizedOption: {:?}", error);
        }

        // Test with invalid option type w/ action = discard & send icmp if not multicast
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::DestinationOptions.into());
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(),      // Next Header
            1,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            1,   4, 0, 0, 0, 0,       // Pad6
            255, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action = discard & send icmp
                                      // if destination address is not a multicast
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed succesfully with an unrecognized option type");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
            pointer,
            must_send_icmp,
            header_len,
            action,
        } = error
        {
            assert_eq!(pointer, 8);
            assert!(must_send_icmp);
            assert_eq!(header_len, 0);
            assert_eq!(action, ExtensionHeaderOptionAction::DiscardPacketSendICMPNoMulticast);
        } else {
            panic!("Should have matched with UnrecognizedOption: {:?}", error);
        }
    }

    #[test]
    fn test_multiple_ext_hdrs() {
        // Test parsing of multiple extension headers.
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
        #[rustfmt::skip]
        let buffer = [
            // HopByHop Options Extension Header
            Ipv6ExtHdrType::Routing.into(), // Next Header
            0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                       // Pad1
            1, 0,                    // Pad2
            1, 1, 0,                 // Pad3

            // Routing Extension Header
            Ipv6ExtHdrType::DestinationOptions.into(), // Next Header
            4,                                  // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                                  // Routing Type
            1,                                  // Segments Left
            0, 0, 0, 0,                         // Reserved
            // Addresses for Routing Header w/ Type 0
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

            // Destination Options Extension Header
            IpProto::Tcp.into(),     // Next Header
            1,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                       // Pad1
            1,  0,                   // Pad2
            1,  1, 0,                // Pad3
            63, 6, 0, 0, 0, 0, 0, 0, // Unrecognized type w/ action = discard
        ];
        let ext_hdrs =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .unwrap();

        let ext_hdrs: Vec<Ipv6ExtensionHeader> = ext_hdrs.iter().collect();
        assert_eq!(ext_hdrs.len(), 3);

        // Check first extension header (hop-by-hop options)
        assert_eq!(ext_hdrs[0].next_header, Ipv6ExtHdrType::Routing.into());
        if let Ipv6ExtensionHeaderData::HopByHopOptions { options } = ext_hdrs[0].data() {
            // Everything should have been a NOP/ignore
            assert_eq!(options.iter().count(), 0);
        } else {
            panic!("Should have matched HopByHopOptions: {:?}", ext_hdrs[0].data());
        }

        // Check the second extension header (routing options)
        assert_eq!(ext_hdrs[1].next_header, Ipv6ExtHdrType::DestinationOptions.into());
        if let Ipv6ExtensionHeaderData::Routing { routing_data } = ext_hdrs[1].data() {
            assert_eq!(routing_data.routing_type(), 0);
            assert_eq!(routing_data.segments_left(), 1);

            let RoutingTypeSpecificData::RoutingType0 { addresses } =
                routing_data.type_specific_data();
            let addresses: Vec<LayoutVerified<_, Ipv6Addr>> = addresses.iter().collect();
            assert_eq!(addresses.len(), 2);
            assert_eq!(
                addresses[0].bytes(),
                [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,]
            );
            assert_eq!(
                addresses[1].bytes(),
                [16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,]
            );
        } else {
            panic!("Should have matched Routing: {:?}", ext_hdrs[1].data());
        }

        // Check the third extension header (destination options)
        assert_eq!(ext_hdrs[2].next_header, IpProto::Tcp.into());
        if let Ipv6ExtensionHeaderData::DestinationOptions { options } = ext_hdrs[2].data() {
            // Everything should have been a NOP/ignore except for the unrecognized type
            let options: Vec<DestinationOption> = options.iter().collect();
            assert_eq!(options.len(), 1);
            assert_eq!(options[0].action, ExtensionHeaderOptionAction::SkipAndContinue);
        } else {
            panic!("Should have matched DestinationOptions: {:?}", ext_hdrs[2].data());
        }
    }

    #[test]
    fn test_multiple_ext_hdrs_errs() {
        // Test parsing of multiple extension headers with erros.

        // Test Invalid next header in the second extension header.
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
        #[rustfmt::skip]
        let buffer = [
            // HopByHop Options Extension Header
            Ipv6ExtHdrType::Routing.into(), // Next Header
            0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                       // Pad1
            1, 0,                    // Pad2
            1, 1, 0,                 // Pad3

            // Routing Extension Header
            255,                                // Next Header (Invalid)
            4,                                  // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                                  // Routing Type
            1,                                  // Segments Left
            0, 0, 0, 0,                         // Reserved
            // Addresses for Routing Header w/ Type 0
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

            // Destination Options Extension Header
            IpProto::Tcp.into(),    // Next Header
            1,                      // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                      // Pad1
            1, 0,                   // Pad2
            1, 1, 0,                // Pad3
            1, 6, 0, 0, 0, 0, 0, 0, // Pad8
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed succesfully when the next header was invalid");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedNextHeader {
            pointer,
            must_send_icmp,
            header_len,
        } = error
        {
            assert_eq!(pointer, 8);
            assert!(!must_send_icmp);
            assert_eq!(header_len, 8);
        } else {
            panic!("Should have matched with UnrecognizedNextHeader: {:?}", error);
        }

        // Test HopByHop extension header not being the very first extension header
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
        #[rustfmt::skip]
        let buffer = [
            // Routing Extension Header
            Ipv6ExtHdrType::HopByHopOptions.into(),    // Next Header (Valid but HopByHop restricted to first extension header)
            4,                                  // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                                  // Routing Type
            1,                                  // Segments Left
            0, 0, 0, 0,                         // Reserved
            // Addresses for Routing Header w/ Type 0
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

            // HopByHop Options Extension Header
            Ipv6ExtHdrType::DestinationOptions.into(), // Next Header
            0,                                  // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                                  // Pad1
            1, 0,                               // Pad2
            1, 1, 0,                            // Pad3

            // Destination Options Extension Header
            IpProto::Tcp.into(),    // Next Header
            1,                      // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                      // Pad1
            1, 0,                   // Pad2
            1, 1, 0,                // Pad3
            1, 6, 0, 0, 0, 0, 0, 0, // Pad8
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed succesfully when a hop by hop extension header was not the fist extension header");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedNextHeader {
            pointer,
            must_send_icmp,
            header_len,
        } = error
        {
            assert_eq!(pointer, 0);
            assert!(!must_send_icmp);
            assert_eq!(header_len, 0);
        } else {
            panic!("Should have matched with UnrecognizedNextHeader: {:?}", error);
        }

        // Test parsing of destination options with an unrecognized option type w/ action
        // set to discard and send icmp
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
        #[rustfmt::skip]
        let buffer = [
            // HopByHop Options Extension Header
            Ipv6ExtHdrType::DestinationOptions.into(), // Next Header
            0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                       // Pad1
            1, 0,                    // Pad2
            1, 1, 0,                 // Pad3

            // Destination Options Extension Header
            IpProto::Tcp.into(),      // Next Header
            1,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                        // Pad1
            1,   0,                   // Pad2
            1,   1, 0,                // Pad3
            191, 6, 0, 0, 0, 0, 0, 0, // Unrecognized type w/ action = discard
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed succesfully with an unrecognized destination option type");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
            pointer,
            must_send_icmp,
            header_len,
            action,
        } = error
        {
            assert_eq!(pointer, 16);
            assert!(must_send_icmp);
            assert_eq!(header_len, 8);
            assert_eq!(action, ExtensionHeaderOptionAction::DiscardPacketSendICMP);
        } else {
            panic!("Should have matched with UnrecognizedNextHeader: {:?}", error);
        }
    }
}