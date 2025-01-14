// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use cstr::cstr;
use failure::{format_err, Error, ResultExt};
use fidl_fuchsia_hardware_pty::{DeviceProxy, WindowSize};
use fuchsia_async as fasync;
use fuchsia_zircon::{self as zx, HandleBased, Task};
use parking_lot::Mutex;
use std::{
    ffi::CStr,
    fs::{File, OpenOptions},
    os::unix::io::AsRawFd,
    sync::Arc,
};

/// An object used for interacting with the shell
pub struct Pty {
    // The server side file descriptor. This file is safe to clone.
    server_pty: File,

    // The running shell process. This object will remain None until after the shell is spawned.
    shell_process: Arc<Mutex<Option<zx::Process>>>,
}

impl Pty {
    /// Creates a new instance of the Pty which must later be spawned.
    pub fn new() -> Result<Self, Error> {
        let server_pty = Pty::open_server_pty()?;
        let shell_process = Arc::new(Mutex::new(None));

        Ok(Pty { server_pty, shell_process })
    }

    /// Spawns the Pty. The pty needs to have a valid window size before it can be spawned or the
    /// shell will not respond to any commands.
    pub async fn spawn(&mut self, window_size: WindowSize) -> Result<(), Error> {
        let spawn_fd = self.try_clone_fd().context("unable to clone pty for shell spawn")?;
        let process = await!(Pty::launch_shell(&spawn_fd, &cstr!("/boot/bin/sh")))
            .context("launch shell process")?;

        {
            let mut option = self.shell_process.lock();
            *option = Some(process);
        }

        await!(Pty::set_window_size(&spawn_fd, window_size))
            .context("unable to set initial window size for shell")?;

        Ok(())
    }

    /// Attempts to clone the server side of the file descriptor.
    pub fn try_clone_fd(&self) -> Result<File, Error> {
        let fd = self.server_pty.try_clone()?;
        Ok(fd)
    }

    /// Closes the shell. This method is safe to call multiple times.
    /// The close method will be called automatically when the Pty is dropped.
    pub fn close(&self) -> Result<(), Error> {
        if let Some(process) = self.shell_process.lock().as_ref() {
            process.kill().context("failed to kill shell process")?;
        }

        Ok(())
    }

    /// Sends a message to the shell that the window has been resized.
    pub async fn resize(&self, window_size: WindowSize) -> Result<(), Error> {
        await!(Pty::set_window_size(&self.server_pty, window_size))?;
        Ok(())
    }

    /// Opens the initial server side of the pty.
    fn open_server_pty() -> Result<File, Error> {
        let server_pty = OpenOptions::new().read(true).write(true).open("/dev/misc/ptmx")?;
        fasync::net::set_nonblock(server_pty.as_raw_fd())
            .context("failed to set PTY to non-blocking")?;
        Ok(server_pty)
    }

    /// Launches the shell process by creating the client side of the pty and then spawning the
    /// shell.
    async fn launch_shell(server_pty: &File, command: &CStr) -> Result<zx::Process, Error> {
        let client_pty =
            await!(Pty::open_client_pty(server_pty)).context("unable to create client_pty")?;
        let process = Pty::spawn_shell_process(client_pty, command)
            .context("unable to spawn shell process")?;

        Ok(process)
    }

    /// Creates a File which is suitable to use as the client side of the Pty.
    async fn open_client_pty(server_pty: &File) -> Result<File, Error> {
        let (device_channel, client_channel) = zx::Channel::create()?;

        let server_pty_channel = fdio::clone_channel(server_pty)
            .context("failed to clone channel from server PTY FD")?;
        let server_pty_fidl_channel = fasync::Channel::from_channel(server_pty_channel)
            .context("failed to create FIDL channel from zircon channel")?;

        let device_proxy = DeviceProxy::new(server_pty_fidl_channel);
        await!(device_proxy.open_client(0, device_channel))
            .context("failed to attach PTY to channel")?;

        // convert the client side into a file descriptor. This must be called
        // after the server side has been established.
        let client_pty = fdio::create_fd(client_channel.into())
            .context("failed to create FD from client PTY")?;

        Ok(client_pty)
    }

    /// spawns the shell and transfers the client pty to the process.
    fn spawn_shell_process(client_pty: File, command: &CStr) -> Result<zx::Process, Error> {
        let process = fdio::spawn_etc(
            &zx::Job::from_handle(zx::Handle::invalid()),
            fdio::SpawnOptions::CLONE_ALL - fdio::SpawnOptions::CLONE_STDIO,
            command,
            &[command],
            None,
            &mut [fdio::SpawnAction::transfer_fd(
                client_pty,
                fdio::fdio_sys::FDIO_FLAG_USE_FOR_STDIO as i32,
            )],
        )
        .map_err(|e| format_err!("failed to spawn shell: {:?}", e))?;

        Ok(process)
    }

    pub async fn set_window_size(
        server_pty: &File,
        mut window_size: WindowSize,
    ) -> Result<(), Error> {
        let server_pty_channel = fdio::clone_channel(server_pty)
            .context("failed to clone channel from server PTY FD")?;
        let server_pty_fidl_channel = fasync::Channel::from_channel(server_pty_channel)
            .context("failed to create FIDL channel from zircon channel")?;
        let device_proxy = DeviceProxy::new(server_pty_fidl_channel);

        await!(device_proxy.set_window_size(&mut window_size))
            .context("Unable to resize window")?;
        Ok(())
    }
}

impl Drop for Pty {
    fn drop(&mut self) {
        // avoid crashing if close fails since the process will be cleaned up when process goes out
        // of scope anyway.
        let _ = self.close();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::io::{AsyncReadExt, AsyncWriteExt};

    #[fasync::run_singlethreaded(test)]
    async fn can_create_pty() -> Result<(), Error> {
        let _ = Pty::new()?;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_open_client_pty() -> Result<(), Error> {
        let server_pty = Pty::open_server_pty()?;
        let client_pty = await!(Pty::open_client_pty(&server_pty))?;
        assert!(client_pty.as_raw_fd() > 0);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_open_server_pty() -> Result<(), Error> {
        let server_pty = Pty::open_server_pty()?;
        assert!(server_pty.as_raw_fd() > 0);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_spawn_shell_process() -> Result<(), Error> {
        let server_pty = Pty::open_server_pty()?;
        let process = await!(Pty::launch_shell(&server_pty, &cstr!("/pkg/bin/sh")))?;

        let mut started = false;
        if let Ok(info) = process.info() {
            started = info.started;
        }

        assert_eq!(started, true);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn shell_process_is_spawned() -> Result<(), Error> {
        let pty = await!(spawn_pty());

        let mut started = false;
        let process_ref = pty.shell_process.clone();
        if let Some(process) = process_ref.lock().as_ref() {
            let info = process.info().unwrap();
            started = info.started;
        }
        assert_eq!(started, true);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn shell_is_killed_on_close() -> Result<(), Error> {
        let pty = await!(spawn_pty());

        pty.close()?;

        let mut exited = false;
        let process_ref = pty.shell_process.clone();
        if let Some(process) = process_ref.lock().as_ref() {
            let info = process.info().unwrap();
            exited = info.exited;
        }
        assert_eq!(exited, true);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_safely_call_close_twice() -> Result<(), Error> {
        let pty = await!(spawn_pty());

        pty.close()?;
        pty.close()?;

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_write_to_shell() -> Result<(), Error> {
        let pty = await!(spawn_pty());
        let mut evented_fd = unsafe { fasync::net::EventedFd::new(pty.try_clone_fd()?)? };

        await!(flush(&mut evented_fd))?;

        await!(evented_fd.write_all("a".as_bytes()))?;

        let mut output = [0u8, 4];
        let result = await!(evented_fd.read(&mut output))?;
        assert_eq!(&output[0..result], "a".as_bytes());

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_resize_window() -> Result<(), Error> {
        let pty = await!(spawn_pty());
        await!(pty.resize(WindowSize { width: 400, height: 400 }))?;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn pty_calls_close_on_drop() -> Result<(), Error> {
        let pty = await!(spawn_pty());
        let process_ref = pty.shell_process.clone();

        drop(pty);

        let mut exited = false;

        if let Some(process) = process_ref.lock().as_ref() {
            let info = process.info().unwrap();
            exited = info.exited;
        }
        assert_eq!(exited, true);

        Ok(())
    }

    // Helper utility to flush out the pty. This method is useful for asserting on what is returned
    // from the pty without having to worry about what is sitting in the read queue.
    //
    // This method will read from the pty until it finds a single space. We use this as an
    // indicator that the pty is waiting for input.
    async fn flush(evented_fd: &mut fasync::net::EventedFd<File>) -> Result<(), Error> {
        loop {
            let mut output = [0u8, 16];
            let _ = await!(evented_fd.read(&mut output))?;
            // Look for a space to signal we have reached the end.
            if output.contains(&32) {
                break;
            }
        }

        Ok(())
    }

    async fn spawn_pty() -> Pty {
        let window_size = WindowSize { width: 300 as u32, height: 300 as u32 };
        let mut pty = Pty::new().unwrap();
        let _ = await!(pty.spawn(window_size));
        pty
    }

}
