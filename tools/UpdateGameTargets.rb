#!/usr/bin/env ruby
# ============================================================
# Kotonoha Engine - Pristine Xcode Project Generator from Scratch
# Creates KotonohaEngine.xcodeproj (Core Engine + ImGui),
# KotonohaDesktop.xcodeproj and KotonohaIOS.xcodeproj (Game Apps + modules).
# Uses shared CONFIGURATION_BUILD_DIR and embeds Python with absolute path for DDLC.
# Usage: ruby tools/UpdateGameTargets.rb
# ============================================================

require 'xcodeproj'
require 'pathname'
require 'fileutils'

root = Pathname.new(__dir__).parent.expand_path
xcode_dir = root.join('buildsystems/xcode').expand_path
games_dir = root.join('games').expand_path
shared_build_dir = xcode_dir.join('build/$(CONFIGURATION)').to_s

GAMES = {
  'example'                  => { target: 'KotonohaExample', bundle: 'org.kotonoha.example', std: 'c++17', python: false, modules: [] },
  'amagami'                  => { target: 'Amagami', bundle: 'me.hirameki.kotonoha.amagami', std: 'c++17', python: false, modules: ['modules/providers/amagami', 'modules/vms/amagami'] },
  'zero-no-tsukaima'         => { target: 'ZeroNoTsukaima', bundle: 'me.hirameki.kotonoha.zeronotsukaima', std: 'c++17', python: false, modules: ['modules/providers/zero', 'modules/vms/kag'] },
  'snow-radish-vacation'     => { target: 'SnowRadish', bundle: 'me.hirameki.kotonoha.snowradish', std: 'c++17', python: false, modules: ['modules/providers/adv', 'modules/vms/snow-radish'] },
  'summer-radish-vacation'   => { target: 'SummerRadish', bundle: 'me.hirameki.kotonoha.summerradish', std: 'c++17', python: false, modules: ['modules/providers/adv', 'modules/vms/summer-radish'] },
  'doki-doki-literature-club'=> { target: 'DokiDoki', bundle: 'me.hirameki.kotonoha.dokidokiliteratureclub', std: 'c++20', python: true, modules: ['modules/providers/rpa', 'modules/vms/renpy'] },
  'school-days'              => { target: 'SchoolDays', bundle: 'me.hirameki.kotonoha.schooldays', std: 'c++17', python: false, modules: ['modules/providers/gpk', 'modules/providers/pak', 'modules/providers/cmp', 'modules/vms/kag'] },
  'reallive'                 => { target: 'RealLive', bundle: 'me.hirameki.kotonoha.reallive', std: 'c++17', python: false, modules: ['modules/providers/reallive', 'modules/vms/reallive'] },
  'kirikiri'                 => { target: 'Kirikiri', bundle: 'me.hirameki.kotonoha.kirikiri', std: 'c++17', python: false, modules: ['modules/providers/xp3', 'modules/vms/kag'] },
  'luca-system'              => { target: 'LucaSystem', bundle: 'me.hirameki.kotonoha.lucasystem', std: 'c++17', python: false, modules: ['modules/providers/adv', 'modules/vms/meo'] },
  'pmail'                    => { target: 'Pmail', bundle: 'me.hirameki.kotonoha.pmail', std: 'c++17', python: false, modules: ['modules/providers/pmail', 'modules/providers/adv', 'modules/providers/snow-radish', 'modules/vms/pmail'] }
}.freeze

# Clean up old projects
['KotonohaEngine.xcodeproj', 'KotonohaDesktop.xcodeproj', 'KotonohaIOS.xcodeproj'].each do |proj_name|
  proj_path = xcode_dir.join(proj_name)
  FileUtils.rm_rf(proj_path) if proj_path.exist?
  puts "[Kotonoha] Removed old #{proj_name}"
end

# 1. Create KotonohaEngine.xcodeproj from scratch (Core Engine + ImGui)
engine_proj_path = xcode_dir.join('KotonohaEngine.xcodeproj')
engine_proj = Xcodeproj::Project.new(engine_proj_path.to_s)
engine_target = engine_proj.new_target(:framework, 'KotonohaEngine', :osx, '12.0')
imgui_target = engine_proj.new_target(:framework, 'KotonohaImGui', :osx, '12.0')

engine_target.add_dependency(imgui_target)

lib_src_dir = root.join('lib/src')
engine_group = engine_proj.main_group.new_group('EngineSources')
Dir.glob(lib_src_dir.join('**/*.{cpp,c,m,mm}')).each do |src_file|
  rel_path = Pathname.new(src_file).relative_path_from(engine_proj_path.parent)
  file_ref = engine_group.new_file(rel_path.to_s)
  engine_target.add_file_references([file_ref])
end

imgui_dir = root.join('lib/submodules/imgui')
imgui_group = engine_proj.main_group.new_group('ImGuiSources')
[
  'imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp',
  'backends/imgui_impl_sdl3.cpp', 'backends/imgui_impl_sdlrenderer3.cpp',
  'misc/cpp/imgui_stdlib.cpp'
].each do |imgui_src|
  full_path = imgui_dir.join(imgui_src)
  if full_path.exist?
    rel_path = full_path.relative_path_from(engine_proj_path.parent)
    file_ref = imgui_group.new_file(rel_path.to_s)
    imgui_target.add_file_references([file_ref])
  end
end

vcpkg_macos_lib = root.join('buildsystems/xcode/vcpkg/macos/arm64-osx/lib').to_s
vcpkg_macos_inc = root.join('buildsystems/xcode/vcpkg/macos/arm64-osx/include').to_s

engine_ldflags = [
  '$(inherited)',
  '-framework KotonohaImGui',
  '-ljson-c', '-lass', '-lz', '-lharfbuzz', '-lfribidi', '-lfreetype',
  '-lfontconfig', '-liconv', '-lbz2', '-lbrotlidec', '-lbrotlicommon',
  '-lexpat', '-lpng', '-lunibreak',
  '-framework OpenGL',
  '-framework Cocoa',
  '-framework AudioToolbox',
  '-framework CoreMedia',
  '-framework CoreVideo',
  '-framework VideoToolbox',
  '-lavcodec', '-lavformat', '-lavutil', '-lswresample', '-lswscale'
]

engine_headers = [
  '$(inherited)',
  root.join('lib/include').to_s,
  root.join('lib/submodules/SDL/include').to_s,
  root.join('lib/submodules/SDL_ttf/include').to_s,
  root.join('lib/submodules/imgui').to_s,
  root.join('lib/submodules/imgui/backends').to_s,
  root.join('lib/submodules/imgui/misc/cpp').to_s,
  vcpkg_macos_inc
]
Dir.glob(root.join('modules/providers/*/include')).each { |d| engine_headers << d }
Dir.glob(root.join('modules/vms/*/include')).each { |d| engine_headers << d }

[engine_target, imgui_target].each do |t|
  t.build_configurations.each do |config|
    config.build_settings['CLANG_CXX_LANGUAGE_STANDARD'] = 'c++17'
    config.build_settings['DEFINES_MODULE'] = 'YES'
    config.build_settings['DYLIB_INSTALL_NAME_BASE'] = '@rpath'
    config.build_settings['GENERATE_INFOPLIST_FILE'] = 'YES'
    config.build_settings['MACOSX_DEPLOYMENT_TARGET'] = '12.0'
    config.build_settings['KOTONOHA_ROOT'] = root.to_s
    config.build_settings['CONFIGURATION_BUILD_DIR'] = shared_build_dir
    config.build_settings['HEADER_SEARCH_PATHS'] = engine_headers.uniq
    config.build_settings['FRAMEWORK_SEARCH_PATHS'] = [
      '$(inherited)',
      root.join('lib/submodules/SDL/Xcode/SDL/build/$(CONFIGURATION)').to_s,
      root.join('lib/submodules/SDL_ttf/Xcode/build/$(CONFIGURATION)').to_s,
      'build/$(CONFIGURATION)'
    ]
    config.build_settings['LIBRARY_SEARCH_PATHS'] = ['$(inherited)', vcpkg_macos_lib]
    config.build_settings['OTHER_LDFLAGS'] = t == engine_target ? (engine_ldflags + ['-framework SDL3', '-framework SDL3_ttf']) : ['$(inherited)', '-framework SDL3']
  end
end

engine_proj.save
puts "[Kotonoha] Created KotonohaEngine.xcodeproj from scratch.\n\n"

# 2. Helper to create App projects (Desktop & iOS) from scratch
def create_app_project(proj_path, platform_name, is_ios, games_dir, root, shared_build_dir)
  proj_path = Pathname.new(proj_path).expand_path
  proj = Xcodeproj::Project.new(proj_path.to_s)
  games_group = proj.main_group.new_group('Games')

  GAMES.each do |game_id, info|
    game_path = games_dir.join(game_id)
    next unless game_path.exist?

    target_name = info[:target]
    target = proj.new_target(
      :application,
      target_name,
      is_ios ? :ios : :osx,
      is_ios ? '15.0' : '12.0'
    )

    game_subgroup = games_group.new_group(target_name)

    # Add game source files
    game_src_dir = game_path.join('src')
    if game_src_dir.exist?
      Dir.glob(game_src_dir.join('**/*.{cpp,c,m,mm}')).each do |src_file|
        rel_path = Pathname.new(src_file).relative_path_from(proj_path.parent)
        file_ref = game_subgroup.new_file(rel_path.to_s)
        target.add_file_references([file_ref])
      end
    end

    # Add required module sources for this game
    info[:modules].each do |mod_rel_path|
      mod_dir = root.join(mod_rel_path)
      next unless mod_dir.exist?

      mod_src_dir = mod_dir.join('src')
      if mod_src_dir.exist?
        mod_subgroup = game_subgroup.new_group(Pathname.new(mod_rel_path).basename.to_s)
        Dir.glob(mod_src_dir.join('**/*.{cpp,c}')).each do |mod_src|
          rel_path = Pathname.new(mod_src).relative_path_from(proj_path.parent)
          file_ref = mod_subgroup.new_file(rel_path.to_s)
          target.add_file_references([file_ref])
        end
      end
    end

    target.build_configurations.each do |config|
      config.build_settings['PRODUCT_NAME'] = target_name
      config.build_settings['PRODUCT_BUNDLE_IDENTIFIER'] = info[:bundle]
      config.build_settings['CLANG_CXX_LANGUAGE_STANDARD'] = info[:std]
      config.build_settings['GENERATE_INFOPLIST_FILE'] = 'YES'
      config.build_settings['KOTONOHA_ROOT'] = root.to_s
      config.build_settings['CONFIGURATION_BUILD_DIR'] = shared_build_dir

      vcpkg_lib_path = is_ios ?
        root.join('buildsystems/xcode/vcpkg/simulator/arm64-ios-simulator/lib').to_s :
        root.join('buildsystems/xcode/vcpkg/macos/arm64-osx/lib').to_s

      vcpkg_inc_path = is_ios ?
        root.join('buildsystems/xcode/vcpkg/simulator/arm64-ios-simulator/include').to_s :
        root.join('buildsystems/xcode/vcpkg/macos/arm64-osx/include').to_s

      if is_ios
        config.build_settings['IPHONEOS_DEPLOYMENT_TARGET'] = '15.0'
        config.build_settings['TARGETED_DEVICE_FAMILY'] = '1,2'
      else
        config.build_settings['MACOSX_DEPLOYMENT_TARGET'] = '12.0'
      end

      headers = [
        '$(inherited)',
        root.join('lib/include').to_s,
        root.join('lib/submodules/SDL/include').to_s,
        root.join('lib/submodules/SDL_ttf/include').to_s,
        root.join('lib/submodules/imgui').to_s,
        root.join('lib/submodules/imgui/backends').to_s,
        root.join('lib/submodules/imgui/misc/cpp').to_s,
        root.join("games/#{game_id}/include").to_s,
        root.join("games/#{game_id}/src").to_s,
        vcpkg_inc_path
      ]
      Dir.glob(root.join('modules/providers/*/include')).each { |d| headers << d }
      Dir.glob(root.join('modules/vms/*/include')).each { |d| headers << d }

      framework_paths = [
        '$(inherited)',
        shared_build_dir,
        root.join('lib/submodules/SDL/Xcode/SDL/build/$(CONFIGURATION)').to_s,
        root.join('lib/submodules/SDL_ttf/Xcode/build/$(CONFIGURATION)').to_s,
        vcpkg_lib_path
      ]

      ldflags = [
        '$(inherited)',
        '-framework KotonohaEngine',
        '-framework KotonohaImGui',
        '-framework SDL3',
        '-framework SDL3_ttf',
        '-ljson-c', '-lass', '-lz', '-lharfbuzz', '-lfribidi', '-lfreetype',
        '-lfontconfig', '-liconv', '-lbz2', '-lbrotlidec', '-lbrotlicommon',
        '-lexpat', '-lpng', '-lunibreak'
      ]

      if info[:python]
        python_header_path = is_ios ?
          root.join('buildsystems/xcode/python/iOS/Python.xcframework/ios-arm64_x86_64-simulator/Python.framework/Headers').to_s :
          root.join('buildsystems/xcode/python/macOS/Python.xcframework/macos-arm64_x86_64/Python.framework/Headers').to_s
        headers << python_header_path

        python_fw_path = is_ios ?
          root.join('buildsystems/xcode/python/iOS/Python.xcframework/ios-arm64_x86_64-simulator').to_s :
          root.join('buildsystems/xcode/python/macOS/Python.xcframework/macos-arm64_x86_64').to_s
        framework_paths << python_fw_path

        ldflags << '-framework Python'
      end

      if is_ios
        ldflags += ['-framework UIKit', '-framework Foundation', '-framework Metal']
      else
        ldflags += [
          '-framework Cocoa',
          '-framework OpenGL',
          '-framework AudioToolbox',
          '-framework CoreMedia',
          '-framework CoreVideo',
          '-framework VideoToolbox',
          '-lavcodec', '-lavformat', '-lavutil', '-lswresample', '-lswscale'
        ]
      end

      config.build_settings['HEADER_SEARCH_PATHS'] = headers.uniq
      config.build_settings['FRAMEWORK_SEARCH_PATHS'] = framework_paths.uniq
      config.build_settings['LIBRARY_SEARCH_PATHS'] = ['$(inherited)', shared_build_dir, vcpkg_lib_path].uniq
      config.build_settings['OTHER_LDFLAGS'] = ldflags.uniq
    end

    # Embed frameworks into .app
    embed_phase = target.copy_files_build_phases.find { |p| p.name == 'Embed Frameworks' } ||
                  target.new_copy_files_build_phase('Embed Frameworks')
    embed_phase.symbol_dst_subfolder_spec = :frameworks

    ['KotonohaEngine.framework', 'KotonohaImGui.framework'].each do |fw_name|
      file_ref = proj.files.find { |f| f.path == fw_name } || proj.new_file(fw_name)
      file_ref.source_tree = 'BUILT_PRODUCTS_DIR'
      unless embed_phase.files.any? { |f| f.file_ref == file_ref }
        build_file = proj.new(Xcodeproj::Project::Object::PBXBuildFile)
        build_file.file_ref = file_ref
        embed_phase.files << build_file
      end
    end

    if info[:python]
      py_fw_path = is_ios ?
        root.join('buildsystems/xcode/python/iOS/Python.xcframework/ios-arm64_x86_64-simulator/Python.framework') :
        root.join('buildsystems/xcode/python/macOS/Python.xcframework/macos-arm64_x86_64/Python.framework')
      py_file_ref = proj.files.find { |f| f.real_path == py_fw_path } || proj.new_file(py_fw_path.to_s)
      py_file_ref.source_tree = '<absolute>'
      unless embed_phase.files.any? { |f| f.file_ref == py_file_ref }
        py_build_file = proj.new(Xcodeproj::Project::Object::PBXBuildFile)
        py_build_file.file_ref = py_file_ref
        embed_phase.files << py_build_file
      end
    end

    scheme = Xcodeproj::XCScheme.new
    scheme.add_build_target(target)
    scheme.save_as(proj_path, target_name, true)
    puts "  [+] Configured app target and shared scheme with bundled absolute frameworks: #{target_name} (#{game_id}) [Python: #{info[:python]}]"
  end

  proj.save
  puts "[Kotonoha] Created and saved #{proj_path.basename} from scratch.\n\n"
end

create_app_project(xcode_dir.join('KotonohaDesktop.xcodeproj'), 'Desktop', false, games_dir, root, shared_build_dir)
create_app_project(xcode_dir.join('KotonohaIOS.xcodeproj'), 'iOS', true, games_dir, root, shared_build_dir)

puts "[Kotonoha] Pristine Xcode project generation from scratch completed successfully!"
