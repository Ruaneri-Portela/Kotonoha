#!/usr/bin/env ruby
require 'fileutils'
require 'xcodeproj'
require 'pathname'

ROOT = File.expand_path('..', __dir__)

module XcodeProjectCommon
  module ProjectHelpers
    module_function

    def get_rel_path(abs_path, project_dir)
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

    def self.sdl_products_search_path(project_dir)
      rel_abs = File.join(File.dirname(sdl_abs_path), 'build/$(CONFIGURATION)')
      ProjectHelpers.get_rel_path(rel_abs, project_dir)
    end

    def self.sdl_ttf_products_search_path(project_dir)
      rel_abs = File.join(File.dirname(sdl_ttf_abs_path), 'build/$(CONFIGURATION)')
      ProjectHelpers.get_rel_path(rel_abs, project_dir)
    end

    def self.engine_products_search_path(project_dir)
      rel_abs = File.join(File.dirname(engine_abs_path), 'build/$(CONFIGURATION)/$(CONFIGURATION)')
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

# Desktop specific logic
PROJECT_PATH = File.join(ROOT, 'buildsystems/xcode/KotonohaDesktop.xcodeproj')
PROJECT_DIR = File.dirname(PROJECT_PATH)

FileUtils.rm_rf(PROJECT_PATH)
project = Xcodeproj::Project.new(PROJECT_PATH, false, 70)
project.root_object.attributes['LastUpgradeCheck'] = '1500'

# ---------------------------------------------------------------------------
# Dependency projects
# ---------------------------------------------------------------------------

# Add KotonohaEngine as a dependency project
engine_ref = XcodeProjectCommon::ProjectHelpers.add_project_ref(project, XcodeProjectCommon::Dependencies.engine_abs_path, 'KotonohaEngine.xcodeproj', PROJECT_DIR)

# SDL projects
sdl_ref = XcodeProjectCommon::ProjectHelpers.add_project_ref(project, XcodeProjectCommon::Dependencies.sdl_abs_path, 'SDL.xcodeproj', PROJECT_DIR)
sdl_ttf_ref = XcodeProjectCommon::ProjectHelpers.add_project_ref(project, XcodeProjectCommon::Dependencies.sdl_ttf_abs_path, 'SDL_ttf.xcodeproj', PROJECT_DIR)

# Product groups
dependencies_group = project.main_group.new_group('Dependencies')
sdl_products_group = dependencies_group.new_group('SDL Products')
sdl_ttf_products_group = dependencies_group.new_group('SDL_ttf Products')
engine_products_group = dependencies_group.new_group('Engine Products')

XcodeProjectCommon::ProjectHelpers.add_project_dependency(project, sdl_ref, sdl_products_group)
XcodeProjectCommon::ProjectHelpers.add_project_dependency(project, sdl_ttf_ref, sdl_ttf_products_group)
XcodeProjectCommon::ProjectHelpers.add_project_dependency(project, engine_ref, engine_products_group)

# SDL3 and SDL_ttf products
sdl_product = XcodeProjectCommon::ProjectHelpers.create_subproject_product_ref(project, 'SDL3.framework', 'wrapper.framework', sdl_products_group)
sdl_ttf_product = XcodeProjectCommon::ProjectHelpers.create_subproject_product_ref(project, 'SDL3_ttf.framework', 'wrapper.framework', sdl_ttf_products_group)

# Engine products
engine_framework = XcodeProjectCommon::ProjectHelpers.create_file_ref(project, File.join(File.dirname(XcodeProjectCommon::Dependencies.engine_abs_path), 'build/$(CONFIGURATION)/$(CONFIGURATION)/KotonohaEngine.framework'), 'KotonohaEngine.framework', PROJECT_DIR, 'wrapper.framework')
imgui_framework = XcodeProjectCommon::ProjectHelpers.create_file_ref(project, File.join(File.dirname(XcodeProjectCommon::Dependencies.engine_abs_path), 'build/$(CONFIGURATION)/$(CONFIGURATION)/KotonohaImGui.framework'), 'KotonohaImGui.framework', PROJECT_DIR, 'wrapper.framework')

# Search Paths
sdl_products_search_path = XcodeProjectCommon::Dependencies.sdl_products_search_path(PROJECT_DIR)
sdl_ttf_products_search_path = XcodeProjectCommon::Dependencies.sdl_ttf_products_search_path(PROJECT_DIR)
engine_products_search_path = XcodeProjectCommon::Dependencies.engine_products_search_path(PROJECT_DIR)

# ---------------------------------------------------------------------------
# Source groups
# ---------------------------------------------------------------------------
game_group = project.main_group.new_group('Kotonoha')

# Example game: sources
[
  File.join(ROOT, 'games/example/src/main.cpp'),
  File.join(ROOT, 'games/example/src/DemoGame.cpp'),
  File.join(ROOT, 'games/example/src/DemoStage.cpp'),
  File.join(ROOT, 'games/example/src/DemoStory.cpp')
].each do |path|
  ref = XcodeProjectCommon::ProjectHelpers.create_local_file_ref(project, path, nil, PROJECT_DIR)
  game_group.children << ref
end

assets_path = File.join(ROOT, 'assets')
assets_ref = XcodeProjectCommon::ProjectHelpers.create_local_file_ref(project, assets_path, 'assets', PROJECT_DIR, 'SRCROOT', 'folder')
game_group.children << assets_ref

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
target = project.new_target(:app, 'Kotonoha', :osx, '14.0')
target.product_type = 'com.apple.product-type.application'

# Sources
XcodeProjectCommon::ProjectHelpers.add_files_to_build_phase(target, game_group.children, 'PBXSourcesBuildPhase')

# Frameworks
target.frameworks_build_phase.add_file_reference(engine_framework)
target.frameworks_build_phase.add_file_reference(imgui_framework)
target.frameworks_build_phase.add_file_reference(sdl_product)
target.frameworks_build_phase.add_file_reference(sdl_ttf_product)

XcodeProjectCommon::ProjectHelpers.add_system_frameworks(project, target, ['AudioToolbox', 'CoreMedia', 'CoreVideo', 'VideoToolbox'])

# Embed Frameworks
embed_phase = target.new_copy_files_build_phase('Embed Frameworks')
embed_phase.dst_path = ""
embed_phase.dst_subfolder_spec = "10"
embed_phase.add_file_reference(imgui_framework)
embed_phase.add_file_reference(sdl_product)
embed_phase.add_file_reference(sdl_ttf_product)

# Resources
target.resources_build_phase.add_file_reference(assets_ref)

# Build Settings
target.build_configurations.each do |config|
  settings = {
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
    'CODE_SIGN_STYLE' => 'Automatic',
    'GENERATE_INFOPLIST_FILE' => 'YES',
    'MACOSX_DEPLOYMENT_TARGET' => '14.0',
    'PRODUCT_BUNDLE_IDENTIFIER' => 'me.kotonoha.example.macos',
    'PRODUCT_NAME' => 'Kotonoha',
    'SDKROOT' => 'macosx',
    'HEADER_SEARCH_PATHS' => [
      '\$(inherited)',
      "\$(KOTONOHA_ROOT)/lib/include",
      "\$(KOTONOHA_ROOT)/lib/submodules/SDL/include",
      "\$(KOTONOHA_ROOT)/lib/submodules/SDL_ttf/include",
      "\$(KOTONOHA_VCPKG_PREFIX)/include",
    ],
    'FRAMEWORK_SEARCH_PATHS' => [
      '\$(inherited)',
      engine_products_search_path,
      sdl_products_search_path,
      sdl_ttf_products_search_path,
      "\$(KOTONOHA_VCPKG_PREFIX)/lib"
    ],
    'LIBRARY_SEARCH_PATHS' => [
      '\$(inherited)',
      "\$(KOTONOHA_VCPKG_PREFIX)/lib"
    ],
    'OTHER_LDFLAGS' => [
      '\$(inherited)',
      '-framework OpenGL',
      '-lavcodec',
      '-lavformat',
      '-lavutil',
      '-lswresample',
      '-lswscale',
      '-ljson-c',
      '-lass',
      '-lz',
      '-lharfbuzz',
      '-lfribidi',
      '-lfreetype',
      '-lfontconfig',
      '-liconv',
      '-lbz2',
      '-lbrotlidec',
      '-lbrotlicommon',
      '-lexpat',
      '-lpng',
      '-lunibreak'
    ],
  }
  
  # VCPKG integration
  settings['KOTONOHA_ROOT'] = '\$(PROJECT_DIR)/../..'
  settings['KOTONOHA_VCPKG_ROOT[sdk=macosx*]'] = '\$(KOTONOHA_ROOT)/build/xcode-deps/macos'
  settings['KOTONOHA_VCPKG_TRIPLET[sdk=macosx*]'] = 'arm64-osx'
  settings['KOTONOHA_VCPKG_PREFIX[sdk=macosx*]'] = '\$(KOTONOHA_VCPKG_ROOT)/\$(KOTONOHA_VCPKG_TRIPLET)'

  XcodeProjectCommon::ProjectHelpers.apply_settings(config, settings)
end

project.save
puts "Wrote \#{PROJECT_PATH}"
