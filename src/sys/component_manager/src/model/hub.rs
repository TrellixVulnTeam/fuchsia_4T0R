// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        directory_broker,
        framework::FrameworkCapability,
        model::{
            self,
            addable_directory::{AddableDirectory, AddableDirectoryWithResult},
            error::ModelError,
        },
    },
    cm_rust::{CapabilityPath, FrameworkCapabilityDecl},
    failure::format_err,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{DirectoryProxy, NodeMarker, CLONE_FLAG_SAME_RIGHTS},
    fuchsia_async as fasync,
    fuchsia_vfs_pseudo_fs::{directory, file::simple::read_only},
    fuchsia_zircon as zx,
    futures::{
        future::{AbortHandle, Abortable, BoxFuture},
        lock::Mutex,
    },
    std::{collections::HashMap, sync::Arc},
};

struct HubCapability {
    abs_moniker: model::AbsoluteMoniker,
    capability_path: CapabilityPath,
    instances: Arc<Mutex<HashMap<model::AbsoluteMoniker, Instance>>>,
}

impl HubCapability {
    pub fn new(
        abs_moniker: model::AbsoluteMoniker,
        capability_path: CapabilityPath,
        instances: Arc<Mutex<HashMap<model::AbsoluteMoniker, Instance>>>,
    ) -> Self {
        HubCapability { abs_moniker, capability_path, instances }
    }

    pub async fn open_async(
        &self,
        flags: u32,
        open_mode: u32,
        relative_path: String,
        server_end: zx::Channel,
    ) -> Result<(), ModelError> {
        let mut dir_path = self.capability_path.split();
        if dir_path.is_empty() || dir_path.remove(0) != "hub" {
            return Err(ModelError::unsupported_hook_error(format_err!(
                "HubCapability does not support the capability {}",
                self.capability_path.to_string()
            )));
        }

        dir_path.append(
            &mut relative_path
                .split("/")
                .map(|s| s.to_string())
                .filter(|s| !s.is_empty())
                .collect::<Vec<String>>(),
        );

        let instances_map = await!(self.instances.lock());
        if !instances_map.contains_key(&self.abs_moniker) {
            return Err(ModelError::unsupported_hook_error(format_err!(
                "HubCapability is unable to find Realm \"{}\"",
                self.abs_moniker
            )));
        }
        await!(instances_map[&self.abs_moniker].directory.open_node(
            flags,
            open_mode,
            dir_path,
            ServerEnd::<NodeMarker>::new(server_end),
            &self.abs_moniker,
        ))?;

        Ok(())
    }
}

impl FrameworkCapability for HubCapability {
    fn open(
        &self,
        flags: u32,
        open_mode: u32,
        relative_path: String,
        server_chan: zx::Channel,
    ) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(self.open_async(flags, open_mode, relative_path, server_chan))
    }
}

/// Hub state on an instance of a component.
struct Instance {
    pub abs_moniker: model::AbsoluteMoniker,
    pub component_url: String,
    pub execution: Option<Execution>,
    pub directory: directory::controlled::Controller<'static>,
    pub children_directory: directory::controlled::Controller<'static>,
}

/// The execution state for a component that has started running.
struct Execution {
    pub resolved_url: String,
    pub directory: directory::controlled::Controller<'static>,
}

pub struct Hub {
    instances: Arc<Mutex<HashMap<model::AbsoluteMoniker, Instance>>>,
    /// Called when Hub is dropped to drop pseudodirectory hosting the Hub.
    abort_handle: AbortHandle,
}

impl Drop for Hub {
    fn drop(&mut self) {
        self.abort_handle.abort();
    }
}

impl Hub {
    /// Create a new Hub given a |component_url| and a controller to the root directory.
    pub fn new(
        component_url: String,
        mut root_directory: directory::simple::Simple<'static>,
    ) -> Result<Hub, ModelError> {
        let mut instances_map = HashMap::new();
        let abs_moniker = model::AbsoluteMoniker::root();

        let self_directory =
            Hub::add_instance_if_necessary(&abs_moniker, component_url, &mut instances_map)?
                .expect("Did not create directory.");
        root_directory.add_node("self", self_directory, &abs_moniker)?;

        // Run the hub root directory forever until the component manager is terminated.
        let (abort_handle, abort_registration) = AbortHandle::new_pair();
        let future = Abortable::new(root_directory, abort_registration);
        fasync::spawn(async move {
            let _ = await!(future);
        });

        Ok(Hub { instances: Arc::new(Mutex::new(instances_map)), abort_handle })
    }

    fn add_instance_if_necessary(
        abs_moniker: &model::AbsoluteMoniker,
        component_url: String,
        instance_map: &mut HashMap<model::AbsoluteMoniker, Instance>,
    ) -> Result<Option<directory::controlled::Controlled<'static>>, ModelError> {
        if instance_map.contains_key(&abs_moniker) {
            return Ok(None);
        }

        let (instance_controller, mut instance_controlled) =
            directory::controlled::controlled(directory::simple::empty());

        // Add a 'url' file.
        instance_controlled.add_node(
            "url",
            {
                let url = component_url.clone();
                read_only(move || Ok(url.clone().into_bytes()))
            },
            &abs_moniker,
        )?;

        // Add a children directory.
        let (children_controller, children_controlled) =
            directory::controlled::controlled(directory::simple::empty());
        instance_controlled.add_node("children", children_controlled, &abs_moniker)?;

        instance_map.insert(
            abs_moniker.clone(),
            Instance {
                abs_moniker: abs_moniker.clone(),
                component_url,
                execution: None,
                directory: instance_controller,
                children_directory: children_controller,
            },
        );

        Ok(Some(instance_controlled))
    }

    async fn add_instance_to_parent_if_necessary<'a>(
        abs_moniker: &'a model::AbsoluteMoniker,
        component_url: String,
        mut instances_map: &'a mut HashMap<model::AbsoluteMoniker, Instance>,
    ) -> Result<(), ModelError> {
        if let Some(controlled) =
            Hub::add_instance_if_necessary(&abs_moniker, component_url, &mut instances_map)?
        {
            if let (Some(name), Some(parent_moniker)) = (abs_moniker.name(), abs_moniker.parent()) {
                await!(instances_map[&parent_moniker].children_directory.add_node(
                    &name,
                    controlled,
                    &abs_moniker,
                ))?;
            }
        }
        Ok(())
    }

    fn add_resolved_url_file(
        execution_directory: &mut directory::controlled::Controlled<'static>,
        resolved_url: String,
        abs_moniker: &model::AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        execution_directory.add_node(
            "resolved_url",
            { read_only(move || Ok(resolved_url.clone().into_bytes())) },
            &abs_moniker,
        )?;
        Ok(())
    }

    fn add_in_directory(
        execution_directory: &mut directory::controlled::Controlled<'static>,
        realm_state: &model::RealmState,
        routing_facade: &model::RoutingFacade,
        abs_moniker: &model::AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let execution = realm_state.execution.as_ref().unwrap();
        let decl = realm_state.get_decl();
        let tree = model::DirTree::build_from_uses(
            routing_facade.route_use_fn_factory(),
            &abs_moniker,
            decl.clone(),
        )?;
        let mut in_dir = directory::simple::empty();
        tree.install(&abs_moniker, &mut in_dir)?;
        let pkg_dir = execution.namespace.as_ref().and_then(|n| n.package_dir.as_ref());
        if let Some(pkg_dir) = Self::clone_dir(pkg_dir) {
            in_dir.add_node(
                "pkg",
                directory_broker::DirectoryBroker::from_directory_proxy(pkg_dir),
                &abs_moniker,
            )?;
        }
        execution_directory.add_node("in", in_dir, &abs_moniker)?;
        Ok(())
    }

    fn add_expose_directory(
        execution_directory: &mut directory::controlled::Controlled<'static>,
        realm_state: &model::RealmState,
        routing_facade: &model::RoutingFacade,
        abs_moniker: &model::AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let decl = realm_state.get_decl();
        let tree = model::DirTree::build_from_exposes(
            routing_facade.route_expose_fn_factory(),
            &abs_moniker,
            decl.clone(),
        );
        let mut expose_dir = directory::simple::empty();
        tree.install(&abs_moniker, &mut expose_dir)?;
        execution_directory.add_node("expose", expose_dir, &abs_moniker)?;
        Ok(())
    }

    fn add_out_directory(
        execution_directory: &mut directory::controlled::Controlled<'static>,
        execution: &model::Execution,
        abs_moniker: &model::AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        if let Some(out_dir) = Self::clone_dir(execution.outgoing_dir.as_ref()) {
            execution_directory.add_node(
                "out",
                directory_broker::DirectoryBroker::from_directory_proxy(out_dir),
                &abs_moniker,
            )?;
        }
        Ok(())
    }

    fn add_runtime_directory(
        execution_directory: &mut directory::controlled::Controlled<'static>,
        execution: &model::Execution,
        abs_moniker: &model::AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        if let Some(runtime_dir) = Self::clone_dir(execution.runtime_dir.as_ref()) {
            execution_directory.add_node(
                "runtime",
                directory_broker::DirectoryBroker::from_directory_proxy(runtime_dir),
                &abs_moniker,
            )?;
        }
        Ok(())
    }

    pub async fn on_bind_instance_async<'a>(
        &'a self,
        realm: Arc<model::Realm>,
        realm_state: &'a model::RealmState,
        routing_facade: model::RoutingFacade,
    ) -> Result<(), ModelError> {
        let component_url = realm.component_url.clone();
        let abs_moniker = realm.abs_moniker.clone();
        let mut instances_map = await!(self.instances.lock());

        await!(Self::add_instance_to_parent_if_necessary(
            &abs_moniker,
            component_url,
            &mut instances_map,
        ))?;

        let instance = instances_map
            .get_mut(&abs_moniker)
            .expect(&format!("Unable to find instance {} in map.", abs_moniker));

        // If we haven't already created an execution directory, create one now.
        if instance.execution.is_none() {
            if let Some(execution) = realm_state.execution.as_ref() {
                let (execution_controller, mut execution_controlled) =
                    directory::controlled::controlled(directory::simple::empty());

                let exec = Execution {
                    resolved_url: execution.resolved_url.clone(),
                    directory: execution_controller,
                };
                instance.execution = Some(exec);

                Self::add_resolved_url_file(
                    &mut execution_controlled,
                    execution.resolved_url.clone(),
                    &abs_moniker,
                )?;

                Self::add_in_directory(
                    &mut execution_controlled,
                    realm_state,
                    &routing_facade,
                    &abs_moniker,
                )?;

                Self::add_expose_directory(
                    &mut execution_controlled,
                    realm_state,
                    &routing_facade,
                    &abs_moniker,
                )?;

                Self::add_out_directory(&mut execution_controlled, execution, &abs_moniker)?;

                Self::add_runtime_directory(&mut execution_controlled, execution, &abs_moniker)?;

                await!(instance.directory.add_node("exec", execution_controlled, &abs_moniker))?;
            }
        }

        for child_realm in realm_state.get_child_realms().values() {
            await!(Self::add_instance_to_parent_if_necessary(
                &child_realm.abs_moniker,
                child_realm.component_url.clone(),
                &mut instances_map,
            ))?;
        }

        Ok(())
    }

    async fn on_route_framework_capability_async<'a>(
        &'a self,
        realm: Arc<model::Realm>,
        capability_decl: &'a FrameworkCapabilityDecl,
        capability: Option<Box<dyn FrameworkCapability>>,
    ) -> Result<Option<Box<dyn FrameworkCapability>>, ModelError> {
        // If there is already a capability for this capability then it's not a
        // capability destined for the hub.
        if capability.is_some() {
            return Ok(capability);
        }

        // If this capability is not a directory, then it's not a hub capability.
        let capability_path = match capability_decl {
            FrameworkCapabilityDecl::Directory(source_path) => source_path.clone(),
            _ => return Ok(capability),
        };
        let mut dir_path = capability_path.split();

        // If this capability's source path doesn't begin with 'hub', then it's
        // not a hub capability.
        if dir_path.is_empty() || dir_path.remove(0) != "hub" {
            return Ok(capability);
        }

        Ok(Some(Box::new(HubCapability::new(
            realm.abs_moniker.clone(),
            capability_path,
            self.instances.clone(),
        ))))
    }

    // TODO(fsamuel): We should probably preserve the original error messages
    // instead of dropping them.
    fn clone_dir(dir: Option<&DirectoryProxy>) -> Option<DirectoryProxy> {
        dir.and_then(|d| io_util::clone_directory(d, CLONE_FLAG_SAME_RIGHTS).ok())
    }
}

impl model::Hook for Hub {
    fn on_bind_instance<'a>(
        &'a self,
        realm: Arc<model::Realm>,
        realm_state: &'a model::RealmState,
        routing_facade: model::RoutingFacade,
    ) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(self.on_bind_instance_async(realm, realm_state, routing_facade))
    }

    fn on_add_dynamic_child(&self, _realm: Arc<model::Realm>) -> BoxFuture<Result<(), ModelError>> {
        // TODO: Update the hub with the new child
        Box::pin(async { Ok(()) })
    }

    fn on_remove_dynamic_child(
        &self,
        _realm: Arc<model::Realm>,
    ) -> BoxFuture<Result<(), ModelError>> {
        // TODO: Update the hub with the deleted child
        Box::pin(async { Ok(()) })
    }

    fn on_route_framework_capability<'a>(
        &'a self,
        realm: Arc<model::Realm>,
        capability_decl: &'a FrameworkCapabilityDecl,
        capability: Option<Box<dyn FrameworkCapability>>,
    ) -> BoxFuture<Result<Option<Box<dyn FrameworkCapability>>, ModelError>> {
        Box::pin(self.on_route_framework_capability_async(realm, capability_decl, capability))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::{
            self,
            hub::Hub,
            testing::mocks,
            testing::{
                routing_test_helpers::default_component_decl,
                test_hook::HubInjectionTestHook,
                test_utils::{dir_contains, list_directory, list_directory_recursive, read_file},
            },
        },
        cm_rust::{
            self, CapabilityPath, ChildDecl, ComponentDecl, ExposeDecl, ExposeDirectoryDecl,
            ExposeServiceDecl, ExposeSource, UseDecl, UseDirectoryDecl, UseServiceDecl, UseSource,
        },
        fidl::endpoints::{ClientEnd, ServerEnd},
        fidl_fuchsia_io::{
            DirectoryMarker, DirectoryProxy, NodeMarker, MODE_TYPE_DIRECTORY, OPEN_RIGHT_READABLE,
            OPEN_RIGHT_WRITABLE,
        },
        fidl_fuchsia_sys2 as fsys,
        fuchsia_vfs_pseudo_fs::directory::entry::DirectoryEntry,
        fuchsia_zircon as zx,
        std::{convert::TryFrom, iter, path::Path},
    };

    /// Hosts an out directory with a 'foo' file.
    fn foo_out_dir_fn() -> Box<dyn Fn(ServerEnd<DirectoryMarker>) + Send + Sync> {
        Box::new(move |server_end: ServerEnd<DirectoryMarker>| {
            let mut out_dir = directory::simple::empty();
            // Add a 'foo' file.
            out_dir
                .add_entry("foo", { read_only(move || Ok(b"bar".to_vec())) })
                .map_err(|(s, _)| s)
                .expect("Failed to add 'foo' entry");

            out_dir.open(
                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                MODE_TYPE_DIRECTORY,
                &mut iter::empty(),
                ServerEnd::new(server_end.into_channel()),
            );

            let mut test_dir = directory::simple::empty();
            test_dir
                .add_entry("aaa", { read_only(move || Ok(b"bbb".to_vec())) })
                .map_err(|(s, _)| s)
                .expect("Failed to add 'aaa' entry");
            out_dir
                .add_entry("test", test_dir)
                .map_err(|(s, _)| s)
                .expect("Failed to add 'test' directory.");

            fasync::spawn(async move {
                let _ = await!(out_dir);
            });
        })
    }

    /// Hosts a runtime directory with a 'bleep' file.
    fn bleep_runtime_dir_fn() -> Box<dyn Fn(ServerEnd<DirectoryMarker>) + Send + Sync> {
        Box::new(move |server_end: ServerEnd<DirectoryMarker>| {
            let mut pseudo_dir = directory::simple::empty();
            // Add a 'bleep' file.
            pseudo_dir
                .add_entry("bleep", { read_only(move || Ok(b"blah".to_vec())) })
                .map_err(|(s, _)| s)
                .expect("Failed to add 'bleep' entry");

            pseudo_dir.open(
                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                MODE_TYPE_DIRECTORY,
                &mut iter::empty(),
                ServerEnd::new(server_end.into_channel()),
            );
            fasync::spawn(async move {
                let _ = await!(pseudo_dir);
            });
        })
    }

    type DirectoryCallback = Box<dyn Fn(ServerEnd<DirectoryMarker>) + Send + Sync>;

    struct ComponentDescriptor {
        pub name: String,
        pub decl: ComponentDecl,
        pub host_fn: Option<DirectoryCallback>,
        pub runtime_host_fn: Option<DirectoryCallback>,
    }

    async fn start_component_manager_with_hub(
        root_component_url: String,
        components: Vec<ComponentDescriptor>,
    ) -> (Arc<model::Model>, DirectoryProxy) {
        await!(start_component_manager_with_hub_and_hooks(root_component_url, components, vec![]))
    }

    async fn start_component_manager_with_hub_and_hooks(
        root_component_url: String,
        components: Vec<ComponentDescriptor>,
        mut additional_hooks: model::Hooks,
    ) -> (Arc<model::Model>, DirectoryProxy) {
        let resolved_root_component_url = format!("{}_resolved", root_component_url);
        let mut resolver = model::ResolverRegistry::new();
        let mut runner = mocks::MockRunner::new();
        let mut mock_resolver = mocks::MockResolver::new();
        for component in components.into_iter() {
            mock_resolver.add_component(&component.name, component.decl);
            if let Some(host_fn) = component.host_fn {
                runner.host_fns.insert(resolved_root_component_url.clone(), host_fn);
            }

            if let Some(runtime_host_fn) = component.runtime_host_fn {
                runner
                    .runtime_host_fns
                    .insert(resolved_root_component_url.clone(), runtime_host_fn);
            }
        }
        resolver.register("test".to_string(), Box::new(mock_resolver));

        let mut root_directory = directory::simple::empty();

        let (client_chan, server_chan) = zx::Channel::create().unwrap();
        root_directory.open(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            0,
            &mut iter::empty(),
            ServerEnd::<NodeMarker>::new(server_chan.into()),
        );

        let hub = Arc::new(Hub::new(root_component_url.clone(), root_directory).unwrap());
        let mut hooks: model::Hooks = Vec::new();
        hooks.push(hub);
        hooks.append(&mut additional_hooks);
        let model = Arc::new(model::Model::new(model::ModelParams {
            framework_services: Arc::new(mocks::MockFrameworkServiceHost::new()),
            root_component_url,
            root_resolver_registry: resolver,
            root_default_runner: Arc::new(runner),
            hooks,
            config: model::ModelConfig::default(),
        }));

        let res = await!(model.look_up_and_bind_instance(model::AbsoluteMoniker::root()));
        let expected_res: Result<(), model::ModelError> = Ok(());
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));

        let hub_proxy = ClientEnd::<DirectoryMarker>::new(client_chan)
            .into_proxy()
            .expect("failed to create directory proxy");

        (model, hub_proxy)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn hub_basic() {
        let root_component_url = "test:///root".to_string();
        let (_model, hub_proxy) = await!(start_component_manager_with_hub(
            root_component_url.clone(),
            vec![
                ComponentDescriptor {
                    name: "root".to_string(),
                    decl: ComponentDecl {
                        children: vec![ChildDecl {
                            name: "a".to_string(),
                            url: "test:///a".to_string(),
                            startup: fsys::StartupMode::Lazy,
                        }],
                        ..default_component_decl()
                    },
                    host_fn: None,
                    runtime_host_fn: None,
                },
                ComponentDescriptor {
                    name: "a".to_string(),
                    decl: ComponentDecl { children: vec![], ..default_component_decl() },
                    host_fn: None,
                    runtime_host_fn: None,
                },
            ],
        ));

        assert_eq!(root_component_url, await!(read_file(&hub_proxy, "self/url")));
        assert_eq!(
            format!("{}_resolved", root_component_url),
            await!(read_file(&hub_proxy, "self/exec/resolved_url"))
        );
        assert_eq!("test:///a", await!(read_file(&hub_proxy, "self/children/a/url")));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn hub_out_directory() {
        let root_component_url = "test:///root".to_string();
        let (_model, hub_proxy) = await!(start_component_manager_with_hub(
            root_component_url.clone(),
            vec![ComponentDescriptor {
                name: "root".to_string(),
                decl: ComponentDecl {
                    children: vec![ChildDecl {
                        name: "a".to_string(),
                        url: "test:///a".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    }],
                    ..default_component_decl()
                },
                host_fn: Some(foo_out_dir_fn()),
                runtime_host_fn: None,
            }],
        ));

        assert!(await!(dir_contains(&hub_proxy, "self/exec", "out")));
        assert!(await!(dir_contains(&hub_proxy, "self/exec/out", "foo")));
        assert!(await!(dir_contains(&hub_proxy, "self/exec/out/test", "aaa")));
        assert_eq!("bar", await!(read_file(&hub_proxy, "self/exec/out/foo")));
        assert_eq!("bbb", await!(read_file(&hub_proxy, "self/exec/out/test/aaa")));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn hub_runtime_directory() {
        let root_component_url = "test:///root".to_string();
        let (_model, hub_proxy) = await!(start_component_manager_with_hub(
            root_component_url.clone(),
            vec![ComponentDescriptor {
                name: "root".to_string(),
                decl: ComponentDecl {
                    children: vec![ChildDecl {
                        name: "a".to_string(),
                        url: "test:///a".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    }],
                    ..default_component_decl()
                },
                host_fn: None,
                runtime_host_fn: Some(bleep_runtime_dir_fn()),
            }],
        ));

        assert_eq!("blah", await!(read_file(&hub_proxy, "self/exec/runtime/bleep")));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn hub_test_hook_interception() {
        let root_component_url = "test:///root".to_string();
        let (_model, hub_proxy) = await!(start_component_manager_with_hub_and_hooks(
            root_component_url.clone(),
            vec![ComponentDescriptor {
                name: "root".to_string(),
                decl: ComponentDecl {
                    children: vec![ChildDecl {
                        name: "a".to_string(),
                        url: "test:///a".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    }],
                    uses: vec![UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Framework,
                        source_path: CapabilityPath::try_from("/hub").unwrap(),
                        target_path: CapabilityPath::try_from("/hub").unwrap(),
                    })],
                    ..default_component_decl()
                },
                host_fn: None,
                runtime_host_fn: None,
            }],
            vec![Arc::new(HubInjectionTestHook::new())],
        ));

        let in_dir = io_util::open_directory(
            &hub_proxy,
            &Path::new("self/exec/in"),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .expect("Failed to open directory");
        assert_eq!(vec!["hub"], await!(list_directory(&in_dir)));

        let scoped_hub_dir_proxy = io_util::open_directory(
            &hub_proxy,
            &Path::new("self/exec/in/hub"),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .expect("Failed to open directory");
        // There are no out or runtime directories because there is no program running.
        assert_eq!(vec!["old_hub"], await!(list_directory(&scoped_hub_dir_proxy)));

        let old_hub_dir_proxy = io_util::open_directory(
            &hub_proxy,
            &Path::new("self/exec/in/hub/old_hub/exec"),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .expect("Failed to open directory");
        // There are no out or runtime directories because there is no program running.
        assert_eq!(
            vec!["expose", "in", "resolved_url"],
            await!(list_directory(&old_hub_dir_proxy))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn hub_in_directory() {
        let root_component_url = "test:///root".to_string();
        let (_model, hub_proxy) = await!(start_component_manager_with_hub(
            root_component_url.clone(),
            vec![ComponentDescriptor {
                name: "root".to_string(),
                decl: ComponentDecl {
                    children: vec![ChildDecl {
                        name: "a".to_string(),
                        url: "test:///a".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    }],
                    uses: vec![
                        UseDecl::Directory(UseDirectoryDecl {
                            source: UseSource::Framework,
                            source_path: CapabilityPath::try_from("/hub/exec").unwrap(),
                            target_path: CapabilityPath::try_from("/hub").unwrap(),
                        }),
                        UseDecl::Service(UseServiceDecl {
                            source: UseSource::Realm,
                            source_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                            target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        }),
                        UseDecl::Directory(UseDirectoryDecl {
                            source: UseSource::Realm,
                            source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                            target_path: CapabilityPath::try_from("/data/bar").unwrap(),
                        }),
                    ],
                    ..default_component_decl()
                },
                host_fn: None,
                runtime_host_fn: None,
            }],
        ));

        let in_dir = io_util::open_directory(
            &hub_proxy,
            &Path::new("self/exec/in"),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .expect("Failed to open directory");
        assert_eq!(vec!["data", "hub", "svc"], await!(list_directory(&in_dir)));

        let scoped_hub_dir_proxy = io_util::open_directory(
            &hub_proxy,
            &Path::new("self/exec/in/hub"),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .expect("Failed to open directory");
        // There are no out or runtime directories because there is no program running.
        assert_eq!(
            vec!["expose", "in", "resolved_url"],
            await!(list_directory(&scoped_hub_dir_proxy))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn hub_expose_directory() {
        let root_component_url = "test:///root".to_string();
        let (_model, hub_proxy) = await!(start_component_manager_with_hub(
            root_component_url.clone(),
            vec![ComponentDescriptor {
                name: "root".to_string(),
                decl: ComponentDecl {
                    children: vec![ChildDecl {
                        name: "a".to_string(),
                        url: "test:///a".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    }],
                    exposes: vec![
                        ExposeDecl::Service(ExposeServiceDecl {
                            source: ExposeSource::Self_,
                            source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                            target_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                        }),
                        ExposeDecl::Directory(ExposeDirectoryDecl {
                            source: ExposeSource::Self_,
                            source_path: CapabilityPath::try_from("/data/baz").unwrap(),
                            target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        }),
                    ],
                    ..default_component_decl()
                },
                host_fn: None,
                runtime_host_fn: None,
            }],
        ));

        let expose_dir = io_util::open_directory(
            &hub_proxy,
            &Path::new("self/exec/expose"),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .expect("Failed to open directory");
        assert_eq!(vec!["data/hippo", "svc/bar"], await!(list_directory_recursive(&expose_dir)));
    }
}
