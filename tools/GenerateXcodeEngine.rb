#!/usr/bin/env ruby
require 'fileutils'
require 'xcodeproj'
require 'pathname'

ROOT = File.expand_path('..', __dir__)

module XcodeProjectCommon
  module ProjectHelpers
    module_function

    def get_rel_path(abs_path, project_dir)
      puts "DEBUG: abs_path=#{abs_path}, project_dir=#{project_dir}"
      raise "abs_path is empty" if abs_path.nil? || abs_path.empty?
      Pathname.new(abs_path).relative_path_from(Pathname.new(project_dir)).to_s
    end

    def apply_settings(config, settings)
      settings.each { |key, value| config.build_settings[key] = value }
    end

    def add_files_to_build_phase(target, file_refs, phase_type)
      return if file_refs.nil? || file_refs.empty?
      phase = target.build_phases.find { |ph| ph.isa == phase_type }
      return unless phase
      file_refs.each do |ref|
        phase.add_file_reference(ref)
      end
    end

    def create_file_ref(project, abs_path, name, project_dir, source_tree = 'SRCROOT', file_type = nil)
      rel_path = get_rel_path(abs_path, project_dir)
      ref = project.new(Xcodeproj::Project::Object::PBXFileReference)
      ref.name = name || File.basename(abs_path)
      ref.path = rel_path
      ref.source_tree = source_tree
      ref.last_known_file_type = file_type if file_type
      ref
    end

    def create_subproject_product_ref(project, name, type = 'wrapper.framework', parent = nil)
      ref = project.new(Xcodeproj::Project::Object::PBXFileReference)
      ref.name = name
      ref.path = name
      ref.source_tree = 'BUILT_PRODUCTS_DIR'
      ref.last_known_file_type = type
      parent.children << ref if parent
      ref
    end

    def create_local_file_ref(project, abs_path, name, project_dir, source_tree = 'SRCROOT', file_type = nil)
      create_file_ref(project, abs_path, name, project_dir, source_tree, file_type)
    end

    def add_system_frameworks(project, target, names)
      names.each do |name|
        ref = project.new(Xcodeproj::Project::Object::PBXFileReference)
        ref.name = "#{name}.framework"
        ref.path = "System/Library/Frameworks/#{name}.framework"
        ref.source_tree = 'SDKROOT'
        ref.last_known_file_type = 'wrapper.framework'
        project.frameworks_group.children << ref
        target.frameworks_build_phase.add_file_reference(ref)
      end
    end

    def add_project_ref(project, abs_path, name, project_dir)
      ref = project.new(Xcodeproj::Project::Object::PBXFileReference)
      ref.last_known_file_type = 'wrapper.pb-project'
      ref.name = name
      rel_path = get_rel_path(abs_path, project_dir)
      ref.path = rel_path
      ref.source_tree = 'SRCROOT'
      project.main_group.children << ref
      ref
    end

    def add_project_dependency(project, project_ref, product_group)
      attribute = project.root_object.class.references_by_keys_attributes.find do |attr|
        attr.name == :project_references
      end
      dictionary = Xcodeproj::Project::ObjectDictionary.new(attribute, project.root_object)
      dictionary[:project_ref] = project_ref
      dictionary[:product_group] = product_group
      project.root_object.project_references << dictionary
    end
  end

  module Dependencies
    def self.sdl_abs_path
      File.join(ROOT, 'lib/submodules/SDL/Xcode/SDL/SDL.xcodeproj')
    end

    def self.sdl_ttf_abs_path
      File.join(ROOT, 'lib/submodules/SDL_ttf/Xcode/SDL_ttf.xcodeproj')
    end

    def self.engine_abs_path
      File.join(ROOT, 'buildsystems/xcode/KotonohaEngine.xcodeproj')
    end

    def self.sdl_products_search_path_macos(project_dir, sdl_abs_path, sdl_ttf_abs_path)
      sdl_rel = ProjectHelpers.get_rel_path(File.join(File.dirname(sdl_abs_path), 'build/$(CONFIGURATION)'), project_dir)
      sdl_ttf_rel = ProjectHelpers.get_rel_path(File.join(File.dirname(sdl_ttf_abs_path), 'build/$(CONFIGURATION)'), project_dir)
      [sdl_rel, sdl_ttf_rel]
    end

    def self.sdl_products_search_path_ios(project_dir, sdl_abs_path, sdl_ttf_abs_path)
      sdl_rel = ProjectHelpers.get_rel_path(File.join(File.dirname(sdl_abs_path), 'build/Debug-iphoneos'), project_dir)
      sdl_ttf_rel = ProjectHelpers.get_rel_path(File.join(File.dirname(sdl_ttf_abs_path), 'build/Debug-iphoneos'), project_dir)
      [sdl_rel, sdl_ttf_rel]
    end

    def self.engine_products_search_path(project_dir)
      rel_abs = File.join(File.dirname(engine_abs_path), 'build/$(CONFIGURATION)')
      ProjectHelpers.get_rel_path(rel_abs, project_dir)
    end
  end

  module LinkerFlags
    def self.common
      %w[
        -lavformat -lavcodec -lswresample -lswscale -lavutil -ljson-c -lz -liconv
        -lass -lfribidi -lunibreak -lfontconfig -lexpat -lharfbuzz -lfreetype
        -lpng16 -lbz2 -lbrotlidec -lbrotlicommon
      ]
    end
  end
end

# Engine specific logic
PROJECT_PATH = File.join(ROOT, 'buildsystems/xcode/KotonohaEngine.xcodeproj')
PROJECT_DIR = File.dirname(PROJECT_PATH)

COMMON_LINKER_FLAGS = XcodeProjectCommon::LinkerFlags.common
SYSTEM_FRAMEWORKS = %w[
  Foundation Metal VideoToolbox CoreVideo CoreMedia CoreFoundation AudioToolbox
].freeze

FileUtils.rm_rf(PROJECT_PATH)
project = Xcodeproj::Project.new(PROJECT_PATH, false, 56)
project.root_object.attributes['LastUpgradeCheck'] = '1600'

sdl_abs_path = XcodeProjectCommon::Dependencies.sdl_abs_path
sdl_ttf_abs_path = XcodeProjectCommon::Dependencies.sdl_ttf_abs_path

raise "Could not find SDL.xcodeproj at #{sdl_abs_path}" unless File.exist?(sdl_abs_path)
raise "Could not find SDL_ttf.xcodeproj at #{sdl_ttf_abs_path}" unless File.exist?(sdl_ttf_abs_path)

sdl_rel_path = XcodeProjectCommon::ProjectHelpers.get_rel_path(sdl_abs_path, PROJECT_DIR)
sdl_products_search_path = XcodeProjectCommon::ProjectHelpers.get_rel_path(File.join(File.dirname(sdl_abs_path), 'build/$(CONFIGURATION)'), PROJECT_DIR)
sdl_ttf_rel_path = XcodeProjectCommon::ProjectHelpers.get_rel_path(sdl_ttf_abs_path, PROJECT_DIR)
sdl_ttf_products_search_path = XcodeProjectCommon::ProjectHelpers.get_rel_path(File.join(File.dirname(sdl_ttf_abs_path), 'build/$(CONFIGURATION)'), PROJECT_DIR)

sdl_ref = XcodeProjectCommon::ProjectHelpers.add_project_ref(project, sdl_abs_path, 'SDL.xcodeproj', PROJECT_DIR)
sdl_ttf_ref = XcodeProjectCommon::ProjectHelpers.add_project_ref(project, sdl_ttf_abs_path, 'SDL_ttf.xcodeproj', PROJECT_DIR)

dependencies_group = project.main_group.new_group('Dependencies')
sdl_products_group_macos = dependencies_group.new_group('SDL Products (macOS)')
sdl_ttf_products_group_macos = dependencies_group.new_group('SDL_ttf Products (macOS)')
sdl_products_group_ios = dependencies_group.new_group('SDL Products (iOS)')
sdl_ttf_products_group_ios = dependencies_group.new_group('SDL_ttf Products (iOS)')

sdl_mac_search_paths = XcodeProjectCommon::Dependencies.sdl_products_search_path_macos(PROJECT_DIR, sdl_abs_path, sdl_ttf_abs_path)
sdl_ios_search_paths = XcodeProjectCommon::Dependencies.sdl_products_search_path_ios(PROJECT_DIR, sdl_abs_path, sdl_ttf_abs_path)

sdl_framework_search_paths = ["$(inherited)", sdl_mac_search_paths[0], sdl_mac_search_paths[1]]
sdl_framework_search_paths_iphoneos = ["$(inherited)", sdl_ios_search_paths[0], sdl_ios_search_paths[1]]

# Compatibility for line 349
sdl_products_search_path = sdl_mac_search_paths[0]
sdl_ttf_products_search_path = sdl_mac_search_paths[1]

XcodeProjectCommon::ProjectHelpers.add_project_dependency(project, sdl_ref, sdl_products_group_macos)
XcodeProjectCommon::ProjectHelpers.add_project_dependency(project, sdl_ttf_ref, sdl_ttf_products_group_macos)
XcodeProjectCommon::ProjectHelpers.add_project_dependency(project, sdl_ref, sdl_products_group_ios)
XcodeProjectCommon::ProjectHelpers.add_project_dependency(project, sdl_ttf_ref, sdl_ttf_products_group_ios)

SDL3_TARGET_ID = 'BECDF5FE0761BA81005FE872'
SDL3_TTF_TARGET_ID = 'BE48FD5D07AFA17000BB41DA'

# macOS products
sdl_product_macos = XcodeProjectCommon::ProjectHelpers.create_file_ref(project, File.join(File.dirname(sdl_abs_path), 'build/$(CONFIGURATION)/SDL3.framework'), 'SDL3.framework', PROJECT_DIR, 'wrapper.framework')
sdl_products_group_macos.children << sdl_product_macos
sdl_ttf_product_macos = XcodeProjectCommon::ProjectHelpers.create_file_ref(project, File.join(File.dirname(sdl_ttf_abs_path), 'build/$(CONFIGURATION)/SDL3_ttf.framework'), 'SDL3_ttf.framework', PROJECT_DIR, 'wrapper.framework')
sdl_ttf_products_group_macos.children << sdl_ttf_product_macos

# iOS products
sdl_product_ios = XcodeProjectCommon::ProjectHelpers.create_file_ref(project, File.join(File.dirname(sdl_abs_path), 'build/Debug-iphoneos/SDL3.framework'), 'SDL3.framework', PROJECT_DIR, 'wrapper.framework')
sdl_products_group_ios.children << sdl_product_ios
sdl_ttf_product_ios = XcodeProjectCommon::ProjectHelpers.create_file_ref(project, File.join(File.dirname(sdl_ttf_abs_path), 'build/Debug-iphoneos/SDL3_ttf.framework'), 'SDL3_ttf.framework', PROJECT_DIR, 'wrapper.framework')
sdl_ttf_products_group_ios.children << sdl_ttf_product_ios

# ---------------------------------------------------------------------------
# Source groups
# ---------------------------------------------------------------------------
lib_group = project.main_group.new_group('lib')

# 1. Engine Sources
engine_src_group = lib_group.new_group('src')
engine_src_refs = []
engine_header_refs = []

Dir.glob(File.join(ROOT, 'lib/src/**/*.{c,cpp,m,mm,h,hpp,hh}')) .each do |path|
  rel_name = Pathname.new(path).relative_path_from(Pathname.new(File.join(ROOT, 'lib/src'))).to_s
  ref = XcodeProjectCommon::ProjectHelpers.create_file_ref(project, path, rel_name, PROJECT_DIR, 'SRCROOT')
  engine_src_group.children << ref
  if path.match?(/\.(c|cpp|m|mm)$/)
    engine_src_refs << ref
  elsif path.match?(/\.(h|hpp|hh)$/)
    engine_header_refs << ref
  end
end

# 2. ImGui
imgui_group = lib_group.new_group('submodules/imgui')
imgui_files_rel = %w[
  imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp
  backends/imgui_impl_sdl3.cpp backends/imgui_impl_sdlrenderer3.cpp
  misc/cpp/imgui_stdlib.cpp
]
imgui_refs = imgui_files_rel.map do |f|
  abs_path = File.join(ROOT, 'lib/submodules/imgui', f)
  ref = XcodeProjectCommon::ProjectHelpers.create_file_ref(project, abs_path, f, PROJECT_DIR, 'SRCROOT')
  imgui_group.children << ref
  ref
end

# 3. Modules
modules_group = project.main_group.new_group('modules')
provider_groups = {}
{
  'ADV' => 'providers/adv', 'Amagami' => 'providers/amagami', 'CMP' => 'providers/cmp',
  'GPK' => 'providers/gpk', 'PAK' => 'providers/pak', 'RealLive' => 'providers/reallive',
  'RPA' => 'providers/rpa', 'XP3' => 'providers/xp3', 'Zero' => 'providers/zero'
}.each do |name, rel_path_in_modules|
  group = modules_group.new_group("Provider\#{name}")
  
  inc_group = group.new_group('include')
  inc_refs = Dir.glob(File.join(ROOT, "modules/\#{rel_path_in_modules}/include/**/*.{h,hpp}")).map do |p|
    rel_name = Pathname.new(p).relative_path_from(Pathname.new(File.join(ROOT, "modules/\#{rel_path_in_modules}/include"))).to_s
    ref = XcodeProjectCommon::ProjectHelpers.create_file_ref(project, p, rel_name, PROJECT_DIR, 'SRCROOT')
    inc_group.children << ref
    ref
  end
  
  src_group = group.new_group('src')
  src_refs = Dir.glob(File.join(ROOT, "modules/\#{rel_path_in_modules}/src/**/*.{c,cpp}")).map do |p|
    rel_name = Pathname.new(p).relative_path_from(Pathname.new(File.join(ROOT, "modules/\#{rel_path_in_modules}/src"))).to_s
    ref = XcodeProjectCommon::ProjectHelpers.create_file_ref(project, p, rel_name, PROJECT_DIR, 'SRCROOT')
    src_group.children << ref
    ref
  end
  
  provider_groups[name] = { include: inc_refs, sources: src_refs, path: rel_path_in_modules }
end

vm_groups = {}
{
  'Amagami' => 'vms/amagami', 'KAG' => 'vms/kag', 'RealLive' => 'vms/reallive',
  'Renpy' => 'vms/renpy', 'SnowRadish' => 'vms/snow-radish', 'SummerRadish' => 'vms/summer-radish'
}.each do |name, rel_path_in_modules|
  group = modules_group.new_group("Vm\#{name}")
  
  inc_group = group.new_group('include')
  inc_refs = Dir.glob(File.join(ROOT, "modules/\#{rel_path_in_modules}/include/**/*.{h,hpp}")).map do |p|
    rel_name = Pathname.new(p).relative_path_from(Pathname.new(File.join(ROOT, "modules/\#{rel_path_in_modules}/include"))).to_s
    ref = XcodeProjectCommon::ProjectHelpers.create_file_ref(project, p, rel_name, PROJECT_DIR, 'SRCROOT')
    inc_group.children << ref
    ref
  end
  
  src_group = group.new_group('src')
  src_refs = Dir.glob(File.join(ROOT, "modules/\#{rel_path_in_modules}/src/**/*.{c,cpp}")).map do |p|
    rel_name = Pathname.new(p).relative_path_from(Pathname.new(File.join(ROOT, "modules/\#{rel_path_in_modules}/src"))).to_s
    ref = XcodeProjectCommon::ProjectHelpers.create_file_ref(project, p, rel_name, PROJECT_DIR, 'SRCROOT')
    src_group.children << ref
    ref
  end
  
  vm_groups[name] = { include: inc_refs, sources: src_refs, path: rel_path_in_modules }
end

# ---------------------------------------------------------------------------
# Build settings
# ---------------------------------------------------------------------------
  project.build_configurations.each do |config|
    settings = {
      'SYMROOT' => '$(PROJECT_DIR)/build',
      'ALWAYS_SEARCH_USER_PATHS' => 'NO',
      'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
      'CLANG_CXX_LIBRARY' => 'libc++',
      'CLANG_ENABLE_MODULES' => 'YES',
      'CLANG_ENABLE_OBJC_ARC' => 'YES',
      'CLANG_ENABLE_OBJC_WEAK' => 'YES',
      'CLANG_WARN_DOCUMENTATION_COMMENTS' => 'YES',
      'ENABLE_USER_SCRIPT_SANDBOXING' => 'NO',
      'GCC_C_LANGUAGE_STANDARD' => 'gnu11',
      'KOTONOHA_ROOT' => '$(PROJECT_DIR)/../..',
      'KOTONOHA_VCPKG_ROOT[sdk=iphoneos*]' => '$(PROJECT_DIR)/vcpkg/ios',
      'KOTONOHA_VCPKG_ROOT[sdk=iphonesimulator*]' => '$(PROJECT_DIR)/vcpkg/simulator',
      'KOTONOHA_VCPKG_ROOT[sdk=macosx*]' => '$(PROJECT_DIR)/vcpkg/macos',
      'KOTONOHA_VCPKG_TRIPLET[sdk=iphoneos*]' => 'arm64-ios',
      'KOTONOHA_VCPKG_TRIPLET[sdk=iphonesimulator*]' => 'arm64-ios-simulator',
      'KOTONOHA_VCPKG_TRIPLET[sdk=macosx*]' => 'arm64-osx',
      'KOTONOHA_VCPKG_PREFIX' => '$(KOTONOHA_VCPKG_ROOT)/$(KOTONOHA_VCPKG_TRIPLET)',
      'HEADER_SEARCH_PATHS' => [
        '$(inherited)', '$(KOTONOHA_ROOT)/lib/include',
        '$(KOTONOHA_ROOT)/lib/submodules/SDL/include',
        '$(KOTONOHA_ROOT)/lib/submodules/SDL_ttf/include',
        '$(KOTONOHA_ROOT)/lib/submodules/imgui',
        '$(KOTONOHA_ROOT)/lib/submodules/imgui/backends',
        '$(KOTONOHA_ROOT)/lib/submodules/imgui/misc/cpp',
        '$(KOTONOHA_VCPKG_PREFIX)/include',
        '$(KOTONOHA_VCPKG_PREFIX)/include/freetype',
        '$(KOTONOHA_VCPKG_PREFIX)/include/harfbuzz',
        '$(KOTONOHA_VCPKG_PREFIX)/include/plutosvg',
        '$(KOTONOHA_VCPKG_PREFIX)/include/plutovg'
      ],
      'LIBRARY_SEARCH_PATHS' => ['$(inherited)', '$(KOTONOHA_VCPKG_PREFIX)/lib'],
      'OTHER_LDFLAGS' => ['$(inherited)'] + COMMON_LINKER_FLAGS,
      'OTHER_LDFLAGS[sdk=iphoneos*]' => ['$(inherited)'] + COMMON_LINKER_FLAGS + %w[-weak_framework MetalFX -framework OpenGLES],
      'OTHER_LDFLAGS[sdk=iphonesimulator*]' => ['$(inherited)'] + COMMON_LINKER_FLAGS + %w[-framework OpenGLES],
      'OTHER_LDFLAGS[sdk=macosx*]' => ['$(inherited)'] + COMMON_LINKER_FLAGS + %w[-framework OpenGL],
      'IPHONEOS_DEPLOYMENT_TARGET' => '15.0',
      'MACOSX_DEPLOYMENT_TARGET' => '12.0',
      'SDKROOT' => 'iphoneos',
      'SUPPORTED_PLATFORMS' => 'iphoneos iphonesimulator macosx',
      'TARGETED_DEVICE_FAMILY' => '1,2',
      'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'x86_64'
    }
    settings['ONLY_ACTIVE_ARCH'] = 'YES' if config.name == 'Debug'
    settings['ENABLE_TESTABILITY'] = 'YES' if config.name == 'Debug'
    XcodeProjectCommon::ProjectHelpers.apply_settings(config, settings)
  end

# ---------------------------------------------------------------------------
# KotonohaImGui (static library)
# ---------------------------------------------------------------------------
imgui_target = project.new_target(:static_library, 'KotonohaImGui', :ios, '15.0')
imgui_refs.each { |file| imgui_target.source_build_phase.add_file_reference(file) }
imgui_target.dependencies << project.new(Xcodeproj::Project::Object::PBXTargetDependency).tap do |dep|
  dep.name = 'SDL3'
  proxy = project.new(Xcodeproj::Project::Object::PBXContainerItemProxy)
  proxy.container_portal = sdl_ref.uuid
  dep.target_proxy = proxy
end
imgui_target.build_configurations.each do |config|
  XcodeProjectCommon::ProjectHelpers.apply_settings(config, {
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
    'DEFINES_MODULE' => 'NO',
    'GENERATE_INFOPLIST_FILE' => 'NO',
    'SKIP_INSTALL' => 'YES',
    'HEADER_SEARCH_PATHS' => ['$(inherited)', '$(KOTONOHA_ROOT)/lib/submodules/imgui', '$(KOTONOHA_ROOT)/lib/include', '$(KOTONOHA_VCPKG_PREFIX)/include'],
    'LIBRARY_SEARCH_PATHS' => ['$(inherited)', '$(KOTONOHA_VCPKG_PREFIX)/lib']
  })
end

# ---------------------------------------------------------------------------
# KotonohaEngine (static framework)
# ---------------------------------------------------------------------------
engine_target = project.new_target(:framework, 'KotonohaEngine', :ios, '15.0')

XcodeProjectCommon::ProjectHelpers.add_files_to_build_phase(engine_target, engine_src_refs, 'PBXSourcesBuildPhase')
XcodeProjectCommon::ProjectHelpers.add_files_to_build_phase(engine_target, engine_header_refs, 'PBXHeadersBuildPhase')

engine_target.frameworks_build_phase.add_file_reference(sdl_product_ios)
engine_target.frameworks_build_phase.add_file_reference(sdl_ttf_product_ios)

# Link ImGui as a library
engine_target.build_configurations.each do |config|
  config.build_settings['OTHER_LDFLAGS'] ||= []
  config.build_settings['OTHER_LDFLAGS'] << '-lKotonohaImGui'
  config.build_settings['LIBRARY_SEARCH_PATHS'] ||= []
  config.build_settings['LIBRARY_SEARCH_PATHS'] << '$(BUILT_PRODUCTS_DIR)'
end

engine_target.add_dependency(imgui_target)

XcodeProjectCommon::ProjectHelpers.add_system_frameworks(project, engine_target, SYSTEM_FRAMEWORKS)

engine_target.build_configurations.each do |config|
  XcodeProjectCommon::ProjectHelpers.apply_settings(config, {
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
    'CLANG_ENABLE_OBJC_ARC' => 'YES',
    'DEFINES_MODULE' => 'NO',
    'GENERATE_INFOPLIST_FILE' => 'YES',
    'MACH_O_TYPE' => 'staticlib',
    'PRODUCT_BUNDLE_IDENTIFIER' => 'me.kotonoha.engine',
    'PRODUCT_NAME' => 'KotonohaEngine',
    'SKIP_INSTALL' => 'YES',
    'VERSIONING_SYSTEM' => 'apple-generic',
    'VERSION_INFO_PREFIX' => '',
    'DYLIB_COMPATIBILITY_VERSION' => '1',
    'DYLIB_CURRENT_VERSION' => '1',
    'INSTALL_PATH' => '$(LOCAL_LIBRARY_DIR)/Frameworks',
    'GCC_PREPROCESSOR_DEFINITIONS[sdk=iphoneos*]' => ['$(inherited)', 'KOTONOHA_HAS_METALFX=1'],
    'FRAMEWORK_SEARCH_PATHS' => sdl_framework_search_paths + ['$(SRCROOT)'],
    'FRAMEWORK_SEARCH_PATHS[sdk=iphoneos*]' => sdl_framework_search_paths_iphoneos + ['$(SRCROOT)'],
    'HEADER_SEARCH_PATHS' => ['$(inherited)', '$(KOTONOHA_ROOT)/lib/submodules/imgui', '$(KOTONOHA_ROOT)/lib/include', '$(KOTONOHA_VCPKG_PREFIX)/include'],
    'LIBRARY_SEARCH_PATHS' => ['$(inherited)', '$(KOTONOHA_VCPKG_PREFIX)/lib']
  })
end

# ---------------------------------------------------------------------------
# Provider and VM module static libraries
# ---------------------------------------------------------------------------
module_specs = {
  'ProviderADV' => { group: provider_groups['ADV'], includes: [] },
  'ProviderAmagami' => { group: provider_groups['Amagami'], includes: ['$(KOTONOHA_ROOT)/games/amagami/include'] },
  'ProviderCMP' => { group: provider_groups['CMP'], includes: [] },
  'ProviderGPK' => { group: provider_groups['GPK'], includes: [] },
  'ProviderPAK' => { group: provider_groups['PAK'], includes: [] },
  'ProviderRealLive' => { group: provider_groups['RealLive'], includes: ['$(KOTONOHA_ROOT)/games/reallive/include', '$(KOTONOHA_ROOT)/games/reallive/vendor'] },
  'ProviderRPA' => { group: provider_groups['RPA'], includes: ['$(KOTONOHA_ROOT)/games/doki-doki-literature-club/include'] },
  'ProviderXP3' => { group: provider_groups['XP3'], includes: [] },
  'ProviderZero' => { group: provider_groups['Zero'], includes: ['$(KOTONOHA_ROOT)/games/zero-no-tsukaima/include'] },
  'VmAmagami' => { group: vm_groups['Amagami'], includes: ['$(KOTONOHA_ROOT)/games/amagami/include'] },
  'VmKAG' => { group: vm_groups['KAG'], includes: ['$(KOTONOHA_ROOT)/games/kirikiri/include'] },
  'VmRealLive' => { group: vm_groups['RealLive'], includes: ['$(KOTONOHA_ROOT)/games/reallive/include', '$(KOTONOHA_ROOT)/games/reallive/vendor'] },
  'VmRenpy' => { group: vm_groups['Renpy'], includes: ['$(KOTONOHA_ROOT)/games/doki-doki-literature-club/include'] },
  'VmSnowRadish' => { group: vm_groups['SnowRadish'], includes: ['$(KOTONOHA_ROOT)/games/snow-radish-vacation/include'] },
  'VmSummerRadish' => { group: vm_groups['SummerRadish'], includes: ['$(KOTONOHA_ROOT)/modules/providers/cmp/include'] }
}

module_specs.each do |name, spec|
  target = project.new_target(:static_library, name, :ios, '15.0')
  XcodeProjectCommon::ProjectHelpers.add_files_to_build_phase(target, spec[:sources], 'PBXSourcesBuildPhase')
  target.add_dependency(engine_target)
  target.build_configurations.each do |config|
    standard = %w[ProviderRPA VmRenpy].include?(name) ? 'c++20' : 'c++17'
    XcodeProjectCommon::ProjectHelpers.apply_settings(config, {
      'CLANG_CXX_LANGUAGE_STANDARD' => standard,
      'DEFINES_MODULE' => 'NO',
      'GENERATE_INFOPLIST_FILE' => 'YES',
      'PRODUCT_NAME' => name,
      'SKIP_INSTALL' => 'YES',
      'HEADER_SEARCH_PATHS' => ['$(inherited)', '$(KOTONOHA_ROOT)/lib/include', '$(KOTONOHA_ROOT)/modules/\#{spec[:group][:path]}/include'] + spec[:includes],
      'LD_RUNPATH_SEARCH_PATHS' => ['$(inherited)', '@executable_path/Frameworks', '@loader_path/Frameworks']
    })
    if name == 'VmRenpy'
      config.build_settings['HEADER_SEARCH_PATHS[sdk=iphoneos*]'] = [
        '$(inherited)', '$(KOTONOHA_ROOT)/lib/include',
        '$(KOTONOHA_ROOT)/modules/vms/renpy/include',
        '$(KOTONOHA_ROOT)/games/doki-doki-literature-club/include',
        '$(KOTONOHA_ROOT)/build/iOS-deps/Python.xcframework/ios-arm64/Python.framework/Headers'
      ]
      config.build_settings['HEADER_SEARCH_PATHS[sdk=iphonesimulator*]'] = [
        '$(inherited)', '$(KOTONOHA_ROOT)/lib/include',
        '$(KOTONOHA_ROOT)/modules/vms/renpy/include',
        '$(KOTONOHA_ROOT)/games/doki-doki-literature-club/include',
        '$(KOTONOHA_ROOT)/build/iOS-deps/Python.xcframework/ios-arm64_x86_64-simulator/Python.framework/Headers'
      ]
      config.build_settings['HEADER_SEARCH_PATHS[sdk=macosx*]'] = [
        '$(inherited)', '$(KOTONOHA_ROOT)/lib/include',
        '$(KOTONOHA_ROOT)/modules/vms/renpy/include',
        '$(KOTONOHA_ROOT)/games/doki-doki-literature-club/include',
        '/opt/homebrew/opt/python@3.15/Frameworks/Python.framework/Versions/3.15/include/python3.15',
        '/Library/Frameworks/Python.framework/Headers'
      ]
    end
  end
end

project.save
puts "Wrote \#{PROJECT_PATH}"
