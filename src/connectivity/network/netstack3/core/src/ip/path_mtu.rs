// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Module for IP level paths' maximum transmission unit (PMTU) size
//! cache support.

use std::collections::HashMap;
use std::time::Duration;

use log::trace;
use specialize_ip_macro::{specialize_ip, specialize_ip_address};

use crate::ip::{Ip, IpAddress, IpLayerTimerId, IpVersion};
use crate::{Context, EventDispatcher, Instant, TimerId};

/// [RFC 791 section 3.2] requires that an IPv4 node be able to forward
/// datagrams of up to 68 octets without furthur fragmentation. That is,
/// the minimum MTU of an IPv4 path must be 68 bytes. This is because
/// an IPv4 header may be up to 60 octets, and the minimum fragment is
/// 8 octets.
///
/// [RFC 791 section 3.2]: https://tools.ietf.org/html/rfc791#section-3.2
pub(crate) const IPV4_MIN_MTU: u32 = 68;

/// [RFC 8200 section 5] requires that every link in the Internet have an
/// MTU of 1280 octets or greater. Any link that cannot convey a 1280-
/// octet packet in one piece must provide link-specific fragmentation
/// and reassembly at a layer below IPv6.
///
/// [RFC 8200 section 5]: https://tools.ietf.org/html/rfc8200#section-5
pub(crate) const IPV6_MIN_MTU: u32 = 1280;

/// Time between PMTU maintenance operations.
///
/// Maintenance operations are things like resetting cached PMTU
/// data to force restart PMTU discovery to detect increases in
/// a PMTU.
///
/// 1 hour.
// TODO(ghanan): Make this value configurable by runtime options.
const MAINTENANCE_PERIOD: Duration = Duration::from_secs(3600);

/// Time for a PMTU value to be considered stale.
///
/// 3 hours.
// TODO(ghanan): Make this value configurable by runtime options.
const PMTU_STALE_TIMEOUT: Duration = Duration::from_secs(10800);

/// Get the minimum MTU size for a specific IP version, identified by `I`.
#[specialize_ip]
pub(crate) fn min_mtu<I: Ip>() -> u32 {
    #[ipv4]
    let ret = IPV4_MIN_MTU;

    #[ipv6]
    let ret = IPV6_MIN_MTU;

    ret
}

/// Handle a PMTU scheduled timeout.
///
/// See [`IpLayerPathMtuCache::handle_timeout`].
pub(crate) fn handle_pmtu_timeout<D: EventDispatcher>(ctx: &mut Context<D>, ip: IpVersion) {
    let (state, dispatcher) = ctx.state_and_dispatcher();

    match ip {
        IpVersion::V4 => state.ip.v4.path_mtu.handle_timeout(dispatcher),
        IpVersion::V6 => state.ip.v6.path_mtu.handle_timeout(dispatcher),
    }
}

/// Get the PMTU between `src_ip` and `dst_ip`.
///
/// See [`IpLayerPathMtuCache::get_pmtu`].
#[specialize_ip_address]
pub(crate) fn get_pmtu<A: IpAddress, D: EventDispatcher>(
    ctx: &Context<D>,
    src_ip: A,
    dst_ip: A,
) -> Option<u32> {
    #[ipv4addr]
    let ret = ctx.state.ip.v4.path_mtu.get_pmtu(src_ip, dst_ip);

    #[ipv6addr]
    let ret = ctx.state.ip.v6.path_mtu.get_pmtu(src_ip, dst_ip);

    ret
}

/// Update the PMTU between `src_ip` and `dst_ip`.
///
/// See [`IpLayerPathMtuCache::update_pmtu`].
#[specialize_ip_address]
pub(crate) fn update_pmtu<A: IpAddress, D: EventDispatcher>(
    ctx: &mut Context<D>,
    src_ip: A,
    dst_ip: A,
    new_mtu: u32,
) -> Result<Option<u32>, Option<u32>> {
    let (state, dispatcher) = ctx.state_and_dispatcher();

    #[ipv4addr]
    let ret = state.ip.v4.path_mtu.update_pmtu(dispatcher, src_ip, dst_ip, new_mtu);

    #[ipv6addr]
    let ret = state.ip.v6.path_mtu.update_pmtu(dispatcher, src_ip, dst_ip, new_mtu);

    trace!(
        "update_pmtu: Updating the PMTU between src {} and dest {} to {}; was {:?}",
        src_ip,
        dst_ip,
        new_mtu,
        ret
    );

    ret
}

/// Update the PMTU between `src_ip` and `dst_ip` if `new_mtu` is less than
/// the current PMTU and does not violate the minimum MTU size requirements
/// for an IP.
///
/// See [`IpLayerPathMtuCache::update`].
pub(crate) fn update_pmtu_if_less<A: IpAddress, D: EventDispatcher>(
    ctx: &mut Context<D>,
    src_ip: A,
    dst_ip: A,
    new_mtu: u32,
) -> Result<Option<u32>, Option<u32>> {
    let prev_mtu = get_pmtu(ctx, src_ip, dst_ip);

    match prev_mtu {
        // No PMTU exists so update.
        None => update_pmtu(ctx, src_ip, dst_ip, new_mtu),
        // A PMTU exists but it is greater than `new_mtu` so update.
        Some(mtu) if new_mtu < mtu => update_pmtu(ctx, src_ip, dst_ip, new_mtu),
        // A PMTU exists but it is less than or equal to `new_mtu` so no need to update.
        _ => {
            trace!("update_pmtu_if_less: Not updating the PMTU  between src {} and dest {} to {}; is {}", src_ip, dst_ip, new_mtu, prev_mtu.unwrap());
            Ok(prev_mtu)
        }
    }
}

/// The key used to identify a path.
///
/// This is a tuple of (src_ip, dst_ip) as a path is only identified
/// by the source and destination addresses.
// TODO(ghanan): Should device play a part in the key-ing of a path?
#[derive(Copy, Clone, Debug, Hash, PartialEq, Eq)]
pub(crate) struct PathMtuCacheKey<A: IpAddress>(A, A);

impl<A: IpAddress> PathMtuCacheKey<A> {
    fn new(src_ip: A, dst_ip: A) -> Self {
        Self(src_ip, dst_ip)
    }
}

/// Structure to keep track of the PMTU from a (local) source address to
/// some destination address.
type PathMtuCache<A, D> = HashMap<PathMtuCacheKey<A>, PathMtuCacheData<D>>;

/// IP layer PMTU cache data.
pub(crate) struct PathMtuCacheData<D: EventDispatcher> {
    pmtu: u32,
    last_updated: D::Instant,
}

impl<D: EventDispatcher> PathMtuCacheData<D> {
    /// Construct a new `PathMtuCacheData`.
    ///
    /// `last_updated` will be set to the current instant in time as known
    /// by `dispatcher`.
    fn new(dispatcher: &D, pmtu: u32) -> Self {
        Self { pmtu, last_updated: dispatcher.now() }
    }
}

/// IP Layer PMTU cache.
pub(crate) struct IpLayerPathMtuCache<I: Ip, D: EventDispatcher> {
    cache: PathMtuCache<I::Addr, D>,
    next_timer_id: Option<TimerId>,
}

impl<I: Ip, D: EventDispatcher> IpLayerPathMtuCache<I, D> {
    /// Create a new `IpLayerPathMtuCache`.
    pub(crate) fn new() -> Self {
        Self { cache: PathMtuCache::new(), next_timer_id: None }
    }

    /// Get the last updated [`Instant`] when the PMTU between `src_ip`
    /// and `dst_ip` was updated.
    ///
    /// Returns `None` if no PMTU is known by this `IpLayerPathMtuCache`, else
    /// `Some(x)` where `x` is the PMTU's last updated `Instant` in time.
    ///
    /// [`Instant`]: crate::Instant
    pub(crate) fn get_last_updated(&self, src_ip: I::Addr, dst_ip: I::Addr) -> Option<D::Instant> {
        self.cache.get(&PathMtuCacheKey::new(src_ip, dst_ip)).map(|x| x.last_updated)
    }

    /// Get the PMTU between `src_ip` and `dst_ip`.
    ///
    /// Returns `None` if no PMTU is known by this `IpLayerPathMtuCache`, else
    /// `Some(x)` where `x` is the current estimate of the PMTU.
    pub(crate) fn get_pmtu(&self, src_ip: I::Addr, dst_ip: I::Addr) -> Option<u32> {
        self.cache.get(&PathMtuCacheKey::new(src_ip, dst_ip)).map(|x| x.pmtu)
    }

    /// Update the PMTU between `src_ip` and `dst_ip` if `new_mtu` does not violate
    /// IP specific minimum MTU requirements.
    ///
    /// Returns `Err(x)` if the `new_mtu` is less than the minimum MTU for an IP
    /// where the same `x` is returned in the success case (`Ok(x)`). `x` is the
    /// PMTU known by this `IpLayerPathMtuCache` before being updated. `x` will be
    /// `None` if no PMTU is known, else `Some(y)` where `y` is the last estimate
    /// of the PMTU.
    ///
    /// If there is no PMTU maintenance task scheduled yet, `update_pmtu` will
    /// schedule one to happen after a duration of `SCHEDULE_TIMEOUT` from the
    /// current time instant known by `dispatcher`.
    pub(crate) fn update_pmtu(
        &mut self,
        dispatcher: &mut D,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        new_mtu: u32,
    ) -> Result<Option<u32>, Option<u32>> {
        // New MTU must not be smaller than the minimum MTU for an IP.
        if new_mtu < min_mtu::<I>() {
            return Err(self.get_pmtu(src_ip, dst_ip));
        }

        let key = PathMtuCacheKey::new(src_ip, dst_ip);
        let ret = if let Some(data) = self.cache.get_mut(&key) {
            let prev_pmtu = data.pmtu;
            data.pmtu = new_mtu;
            data.last_updated = dispatcher.now();
            Ok(Some(prev_pmtu))
        } else {
            assert!(self.cache.insert(key, PathMtuCacheData::new(dispatcher, new_mtu)).is_none());
            Ok(None)
        };

        // Make sure we have a scheduled task to handle PMTU maintenance.
        // If we don't, create one.
        if self.next_timer_id.is_none() {
            // We are guaranteed that this call will not panic
            // because a panic will only occur if there is already
            // a PMTU maintenance task scheduled. We will only reach
            // here if there is no maintenance task scheduled so
            // we know the panic condition will not be triggered.
            self.create_maintenance_timeout(dispatcher);
        }

        ret
    }

    /// Do scheduled maintenance on PMTU data such as
    /// resetting PMTU values of stale cached values to
    /// restart the PMTU discovery process.
    fn handle_timeout(&mut self, dispatcher: &mut D) {
        // Make sure the timer id we have stored is valid.
        assert_eq!(
            self.next_timer_id.unwrap(),
            IpLayerTimerId::new_pmtu_timeout_timer_id(I::VERSION)
        );

        // Reset the next timer id since we just started the timer
        // that it ids.
        self.next_timer_id = None;

        let curr_time = dispatcher.now();

        // Remove all stale PMTU data to force restart the PMTU discovery
        // process. This will be ok because the next time we try to send
        // a packet to some node, we will update the PMTU with the first
        // known potential PMTU (the first link's (connected to the node
        // attempting PMTU discovery)) PMTU.
        self.cache.retain(|k, v| {
            // We know the call to `duration_since` will not panic because
            // all the entries in the cache should have been updated before
            // this timeout/PMTU maintenance task was run. Therefore, `curr_time`
            // will be greater than `v.last_updated` for all `v`.
            //
            // TODO(ghanan): Add per-path options as per RFC 1981 section 5.3.
            //               Specifically, some links/paths may not need to
            //               have PMTU rediscovered as the PMTU will never
            //               change.
            //
            // TODO(ghanan): Consider not simply deleting all stale PMTU data
            //               as this may cause packets to be dropped every
            //               time the data seems to get stale when really it
            //               is still valid. Considering the use case, PMTU
            //               value changes may be infrequent so it may be
            //               enough to just use a long stale timeout.
            (curr_time.duration_since(v.last_updated) < PMTU_STALE_TIMEOUT)
        });

        // Only attempt to create the next maintenance task if we still
        // have PMTU entries in this cache. If we don't, it would be a
        // waste to schedule the timeout. We will let the next creation
        // of a PMTU entry create the timeout.
        //
        // See `IpLayerPathMtuCache::update_pmtu`.
        if !self.cache.is_empty() {
            // We are guaranteed that this call will not panic
            // because a panic will only occur if there is already
            // a PMTU maintenance task scheduled. We will only reach
            // here after starting a maintenance task and clear the
            // task's `TimerId` so the panic condition will not be
            // triggered.
            self.create_maintenance_timeout(dispatcher);
        }
    }

    /// Create a PMTU maintenance task to occur after a duration of
    /// `MAINTENANCE_PERIOD`.
    ///
    /// # Panics
    ///
    /// Panics if there is already a maintenance task scheduled that
    /// has not yet run.
    fn create_maintenance_timeout(&mut self, dispatcher: &mut D) {
        // Should not create a new job if we already have a maintenance job
        // to be run.
        assert_eq!(self.next_timer_id, None);

        let timer_id = IpLayerTimerId::new_pmtu_timeout_timer_id(I::VERSION);
        assert!(dispatcher.schedule_timeout(MAINTENANCE_PERIOD, timer_id).is_none());
        self.next_timer_id = timer_id.into();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::ip::{Ipv4, Ipv4Addr, Ipv6, Ipv6Addr};
    use crate::testutil::{
        get_dummy_config, run_for, DummyEventDispatcher, DummyEventDispatcherBuilder,
    };

    /// Get the last updated [`Instant`] when the PMTU between `src_ip`
    /// and `dst_ip` was updated.
    ///
    /// See [`IpLayerPathMtuCache::get_last_updated`].
    ///
    /// [`Instant`]: crate::Instant
    #[specialize_ip_address]
    fn get_pmtu_last_updated<A: IpAddress, D: EventDispatcher>(
        ctx: &Context<D>,
        src_ip: A,
        dst_ip: A,
    ) -> Option<D::Instant> {
        #[ipv4addr]
        let ret = ctx.state.ip.v4.path_mtu.get_last_updated(src_ip, dst_ip);

        #[ipv6addr]
        let ret = ctx.state.ip.v6.path_mtu.get_last_updated(src_ip, dst_ip);

        ret
    }

    fn test_ip_path_mtu_cache_ctx<I: Ip>() {
        let dummy_config = get_dummy_config::<I::Addr>();
        let mut ctx = DummyEventDispatcherBuilder::from_config(dummy_config.clone())
            .build::<DummyEventDispatcher>();

        // Nothing in the cache yet
        assert_eq!(get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip), None);
        assert_eq!(
            get_pmtu_last_updated(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip),
            None
        );

        let new_mtu1 = min_mtu::<I>() + 50;
        let start_time = ctx.dispatcher().now();
        let duration = Duration::from_secs(1);

        // Advance time to 1s.
        assert_eq!(run_for(&mut ctx, duration), 0);

        // Update pmtu from local to remote.
        // PMTU should be updated to `new_mtu1` and last updated instant
        // should be updated to the start of the test + 1s.
        assert_eq!(
            update_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip, new_mtu1).unwrap(),
            None
        );

        // Advance time to 2s.
        assert_eq!(run_for(&mut ctx, duration), 0);

        // Make sure the update worked.
        // PMTU should be updated to `new_mtu1` and last updated instant
        // should be updated to the start of the test + 1s (when the
        // update occured.
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            new_mtu1
        );
        assert_eq!(
            get_pmtu_last_updated(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            start_time + duration
        );

        let new_mtu2 = min_mtu::<I>() + 100;

        // Advance time to 3s.
        assert_eq!(run_for(&mut ctx, duration), 0);

        // Updating again should return the last pmtu
        // PMTU should be updated to `new_mtu2` and last updated instant
        // should be updated to the start of the test + 3s.
        assert_eq!(
            update_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip, new_mtu2)
                .unwrap()
                .unwrap(),
            new_mtu1
        );

        // Advance time to 4s.
        assert_eq!(run_for(&mut ctx, duration), 0);

        // Make sure the update worked.
        // PMTU should be updated to `new_mtu2` and last updated instant
        // should be updated to the start of the test + 3s (when the
        // update occured).
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            new_mtu2
        );
        assert_eq!(
            get_pmtu_last_updated(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            start_time + (duration * 3)
        );

        let new_mtu3 = new_mtu2 - 10;

        // Advance time to 5s.
        assert_eq!(run_for(&mut ctx, duration), 0);

        // Make sure update only if new PMTU is less than current (it is).
        // PMTU should be updated to `new_mtu3` and last updated instant
        // should be updated to the start of the test + 5s.
        assert_eq!(
            update_pmtu_if_less(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip, new_mtu3)
                .unwrap()
                .unwrap(),
            new_mtu2
        );

        // Advance time to 6s.
        assert_eq!(run_for(&mut ctx, duration), 0);

        // Make sure the update worked.
        // PMTU should be updated to `new_mtu3` and last updated instant
        // should be updated to the start of the test + 5s (when the
        // update occured).
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            new_mtu3
        );
        let last_updated = start_time + (duration * 5);
        assert_eq!(
            get_pmtu_last_updated(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            last_updated
        );

        let new_mtu4 = new_mtu3 + 50;

        // Advance time to 7s.
        assert_eq!(run_for(&mut ctx, duration), 0);

        // Make sure update only if new PMTU is less than current (it isn't)
        assert_eq!(
            update_pmtu_if_less(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip, new_mtu4)
                .unwrap()
                .unwrap(),
            new_mtu3
        );

        // Advance time to 8s.
        assert_eq!(run_for(&mut ctx, duration), 0);

        // Make sure the update didn't work.
        // PMTU and last updated should not have changed.
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            new_mtu3
        );
        assert_eq!(
            get_pmtu_last_updated(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            last_updated
        );

        let low_mtu = min_mtu::<I>() - 1;

        // Advance time to 9s.
        assert_eq!(run_for(&mut ctx, duration), 0);

        // Updating with mtu value less than the minimum MTU should fail.
        assert_eq!(
            update_pmtu_if_less(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip, low_mtu)
                .unwrap_err()
                .unwrap(),
            new_mtu3
        );

        // Advance time to 10s.
        assert_eq!(run_for(&mut ctx, duration), 0);

        // Make sure the update didn't work.
        // PMTU and last updated should not have changed.
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            new_mtu3
        );
        assert_eq!(
            get_pmtu_last_updated(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            last_updated
        );
    }

    #[test]
    fn test_ipv4_path_mtu_cache_ctx() {
        test_ip_path_mtu_cache_ctx::<Ipv4>();
    }

    #[test]
    fn test_ipv6_path_mtu_cache_ctx() {
        test_ip_path_mtu_cache_ctx::<Ipv6>();
    }

    #[specialize_ip_address]
    fn get_other_ip<A: IpAddress>() -> A {
        #[ipv4addr]
        let ret = Ipv4Addr::new([192, 168, 0, 3]);

        #[ipv6addr]
        let ret = Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 3]);

        ret
    }

    fn test_ip_pmtu_task<I: Ip>() {
        let dummy_config = get_dummy_config::<I::Addr>();
        let mut ctx = DummyEventDispatcherBuilder::from_config(dummy_config.clone())
            .build::<DummyEventDispatcher>();

        // Make sure there are no timers.
        assert_eq!(ctx.dispatcher.timer_events().count(), 0);

        let new_mtu1 = min_mtu::<I>() + 50;
        let start_time = ctx.dispatcher().now();
        let duration = Duration::from_secs(1);

        // Advance time to 1s.
        assert_eq!(run_for(&mut ctx, duration), 0);

        // Update pmtu from local to remote.
        // PMTU should be updated to `new_mtu1` and last updated instant
        // should be updated to the start of the test + 1s.
        assert_eq!(
            update_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip, new_mtu1).unwrap(),
            None
        );

        // Make sure a task got scheduled.
        assert_eq!(ctx.dispatcher.timer_events().count(), 1);

        // Advance time to 2s.
        assert_eq!(run_for(&mut ctx, duration), 0);

        // Make sure the update worked.
        // PMTU should be updated to `new_mtu1` and last updated instant
        // should be updated to the start of the test + 1s (when the
        // update occured.
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            new_mtu1
        );
        assert_eq!(
            get_pmtu_last_updated(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            start_time + duration
        );

        // Advance time to 30mins.
        assert_eq!(run_for(&mut ctx, duration * 1798), 0);

        // Update pmtu from local to another remote.
        // PMTU should be updated to `new_mtu1` and last updated instant
        // should be updated to the start of the test + 1s.
        let other_ip = get_other_ip::<I::Addr>();
        let new_mtu2 = min_mtu::<I>() + 100;
        assert_eq!(update_pmtu(&mut ctx, dummy_config.local_ip, other_ip, new_mtu2).unwrap(), None);

        // Make sure there is still a task scheduled.
        // (we know no timers got triggered because the `run_for`
        // methods returned 0 so far).
        assert_eq!(ctx.dispatcher.timer_events().count(), 1);

        // Make sure the update worked.
        // PMTU should be updated to `new_mtu2` and last updated instant
        // should be updated to the start of the test + 30mins + 2s (when the
        // update occured.
        assert_eq!(get_pmtu(&mut ctx, dummy_config.local_ip, other_ip).unwrap(), new_mtu2);
        assert_eq!(
            get_pmtu_last_updated(&mut ctx, dummy_config.local_ip, other_ip).unwrap(),
            start_time + (duration * 1800)
        );
        // Make sure first update is still in the cache.
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            new_mtu1
        );
        assert_eq!(
            get_pmtu_last_updated(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            start_time + duration
        );

        // Advance time to 1hr + 1s.
        // Should have triggered a timeout.
        assert_eq!(run_for(&mut ctx, duration * 1801), 1);
        // Make sure none of the cache data has been marked as
        // stale and removed.
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            new_mtu1
        );
        assert_eq!(
            get_pmtu_last_updated(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            start_time + duration
        );
        assert_eq!(get_pmtu(&mut ctx, dummy_config.local_ip, other_ip).unwrap(), new_mtu2);
        assert_eq!(
            get_pmtu_last_updated(&mut ctx, dummy_config.local_ip, other_ip).unwrap(),
            start_time + (duration * 1800)
        );
        // Should still have another task scheduled.
        assert_eq!(ctx.dispatcher.timer_events().count(), 1);

        // Advance time to 3hr + 1s.
        // Should have triggered 2 timeouts.
        assert_eq!(run_for(&mut ctx, duration * 7200), 2);
        // Make sure only the earlier PMTU data got marked
        // as stale and removed.
        assert!(get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).is_none());
        assert!(get_pmtu_last_updated(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip)
            .is_none());
        assert_eq!(get_pmtu(&mut ctx, dummy_config.local_ip, other_ip).unwrap(), new_mtu2);
        assert_eq!(
            get_pmtu_last_updated(&mut ctx, dummy_config.local_ip, other_ip).unwrap(),
            start_time + (duration * 1800)
        );
        // Should still have another task scheduled.
        assert_eq!(ctx.dispatcher.timer_events().count(), 1);

        // Advance time to 4hr + 1s.
        // Should have triggered 1 timeouts.
        assert_eq!(run_for(&mut ctx, duration * 3600), 1);
        // Make sure both PMTU data got marked
        // as stale and removed.
        assert!(get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).is_none());
        assert!(get_pmtu_last_updated(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip)
            .is_none());
        assert!(get_pmtu(&mut ctx, dummy_config.local_ip, other_ip).is_none(),);
        assert!(get_pmtu_last_updated(&mut ctx, dummy_config.local_ip, other_ip).is_none(),);
        // Should not have a task scheduled since there is no more PMTU
        // data.
        assert_eq!(ctx.dispatcher.timer_events().count(), 0);
    }

    #[test]
    fn test_ipv4_pmtu_task() {
        test_ip_pmtu_task::<Ipv4>();
    }

    #[test]
    fn test_ipv6_pmtu_task() {
        test_ip_pmtu_task::<Ipv6>();
    }
}
