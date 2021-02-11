project(
    'wpadremap', 'c',
    version: '0.0.1',
    license: 'MIT',
    default_options : [
        'buildtype=release',
        'default_library=static'
    ],
)

git = find_program('git', native: true, required: false)

if not git.found()
    add_project_arguments('-DVERSION="@0@"'.format(meson.project_version()), language: 'c')
else
    git_commit_hash = run_command([git.path(), 'describe', '--always', '--tags']).stdout().strip()
    git_branch = run_command([git.path(), 'rev-parse', '--abbrev-ref', 'HEAD']).stdout().strip()
    version = '"@0@ (" __DATE__ ", branch \'@1@\')"'.format(git_commit_hash, git_branch)
    add_project_arguments('-DVERSION=@0@'.format(version), language: 'cpp')
endif
cc = meson.get_compiler('c')


wayland_client = dependency('wayland-client')
wayland_cursor = dependency('wayland-cursor')
xkbcommon = dependency('xkbcommon')
libinput = dependency('libinput')
libudev = dependency('libudev')
rt = cc.find_library('rt')
systemd = dependency('systemd')


prefix = get_option('prefix')
conf_data = configuration_data()
conf_data.set('prefix', prefix)
conf_data.set('libexecdir',
    join_paths(get_option('prefix'), get_option('libexecdir')))
conf_data.set('systemd_service', '')

if systemd.found()
    systemd_service_file = 'wpadremap.service'
    user_unit_dir = systemd.get_pkgconfig_variable('systemduserunitdir',
        define_variable: ['prefix', get_option('prefix')])
    conf_data.set('systemd_service', 'SystemdService=' + systemd_service_file)

    configure_file(
        configuration: conf_data,
        input: 'wpadremap.service.in',
        output: '@BASENAME@',
        install_dir: user_unit_dir,
    )

endif


src_files = files(
    'main.c',
)

subdir('protocol')

executable(
    meson.project_name(),
    src_files,
    dependencies: [
        client_protos,
        wayland_client,
        wayland_cursor,
        rt,
        xkbcommon,
        libinput,
        libudev,
    ],
    install: true,
    install_dir: get_option('libexecdir'),
)

