#!/usr/bin/env ruby
# Run with: ruby Xcode/UpdateGameTargets.rb (gem install --user-install xcodeproj)
require 'xcodeproj'
require 'pathname'
root = Pathname.new(__dir__).parent
project = Xcodeproj::Project.open(root.join('Xcode/Kotonoha.xcodeproj'))
objects = Xcodeproj::Project::Object
school = project.targets.find { |t| t.name == 'SchoolDays' }
[['Air', 'air', 'AIR', 'air'], ['SummerRadish', 'summer-radish-vacation', 'Summer Radish Vacation!!', 'summerradishvacation']].each do |name, game, title, bundle|
  next if project.targets.any? { |t| t.name == name }
  target = project.new_target(:application, name, :ios, '15.0')
  target.build_configurations.each do |config|
    template = school.build_configurations.find { |c| c.name == config.name }
    config.build_settings = Marshal.load(Marshal.dump(template.build_settings))
    config.build_settings['PRODUCT_NAME'] = name
    config.build_settings['PRODUCT_BUNDLE_IDENTIFIER'] = "me.hirameki.kotonoha.#{bundle}"
    config.build_settings['INFOPLIST_KEY_CFBundleDisplayName'] = title
    config.build_settings['HEADER_SEARCH_PATHS'] = ['$(inherited)', "$(KOTONOHA_ROOT)/games/#{game}/include", "$(KOTONOHA_ROOT)/games/#{game}/src"]
  end
  group = project.main_group.new_group(title)
  sync = project.new(objects::PBXFileSystemSynchronizedRootGroup)
  sync.path = "../games/#{game}/src"
  sync.source_tree = 'SOURCE_ROOT'
  group.children << sync
  target.file_system_synchronized_groups << sync
  engine = project.targets.find { |t| t.name == 'KotonohaEngine' }
  imgui = project.targets.find { |t| t.name == 'ImGui' }
  [engine, imgui].each { |dependency| target.add_dependency(dependency) }
  school.frameworks_build_phase.files.each do |file|
    target.frameworks_build_phase.add_file_reference(file.file_ref)
  end
  embed = target.new_copy_files_build_phase('Embed Frameworks')
  embed.dst_subfolder_spec = '10'
  school.copy_files_build_phases.find { |p| p.name == 'Embed Frameworks' }.files.each do |file|
    added = embed.add_file_reference(file.file_ref)
    added.settings = Marshal.load(Marshal.dump(file.settings)) if file.settings
  end
  [['../assets', 'assets'], ["../games/#{game}/game-assets", 'game-assets']].each do |path, label|
    ref = group.new_file(path, 'SOURCE_ROOT')
    ref.last_known_file_type = 'folder'
    ref.name = label
    target.resources_build_phase.add_file_reference(ref)
  end
  scheme = Xcodeproj::XCScheme.new
  scheme.configure_with_targets(target, nil)
  scheme.save_as(project.path, name, true)
end
air = project.targets.find { |t| t.name == 'Air' }
if air
  air.build_configurations.each do |config|
    paths = Array(config.build_settings['HEADER_SEARCH_PATHS'])
    vendor = '$(KOTONOHA_ROOT)/games/air/vendor'
    config.build_settings['HEADER_SEARCH_PATHS'] = (paths + [vendor]).uniq
    config.build_settings['GCC_CHAR_IS_UNSIGNED_CHAR'] = 'YES'
  end
  unless air.file_system_synchronized_groups.any? { |g| g.path == '../games/air/vendor' }
    vendor = project.new(objects::PBXFileSystemSynchronizedRootGroup)
    vendor.path = '../games/air/vendor'
    vendor.source_tree = 'SOURCE_ROOT'
    project.main_group.children << vendor
    air.file_system_synchronized_groups << vendor
  end
end
project.build_configurations.each do |config|
  settings = config.build_settings
  settings['HEADER_SEARCH_PATHS'] = Array(settings['HEADER_SEARCH_PATHS']).map { |p| p.gsub('/submodules/SDL3/', '/submodules/SDL/').gsub('/submodules/SDL-ttf/', '/submodules/SDL_ttf/') }
end
python = project.files.find { |f| f.path&.include?('Python.xcframework/') && f.path.end_with?('Python.framework') }
if python
  python.path = '../build/ios-deps/Python.xcframework'
  python.name = 'Python.xcframework'
  python.last_known_file_type = 'wrapper.xcframework'
end
python_group = project.main_group.groups.find { |g| g.display_name == 'Bundled Python' } || project.main_group.new_group('Bundled Python')
project.targets.select { |t| ['DokiDoki', 'DdlcIOSScriptSmoke'].include?(t.name) }.each do |target|
  unless target.resources_build_phase.files.any? { |f| f.file_ref&.path == '$(KOTONOHA_PYTHON_NATIVE_DIR)/python' }
    resource = python_group.new_file('$(KOTONOHA_PYTHON_NATIVE_DIR)/python', 'SOURCE_ROOT')
    resource.last_known_file_type = 'folder'
    target.resources_build_phase.add_file_reference(resource)
  end
  embed = target.copy_files_build_phases.find { |p| p.name == 'Embed Frameworks' }
  Dir.glob(root.join('build/ios-deps/python/native/iphoneos/Frameworks/*.framework')).sort.each do |path|
    name = File.basename(path)
    logical = "$(KOTONOHA_PYTHON_NATIVE_DIR)/Frameworks/#{name}"
    next if embed.files.any? { |f| f.file_ref&.path == logical }
    ref = python_group.files.find { |f| f.path == logical } || python_group.new_file(logical, 'SOURCE_ROOT')
    ref.last_known_file_type = 'wrapper.framework'
    file = embed.add_file_reference(ref)
    file.settings = {'ATTRIBUTES' => ['CodeSignOnCopy', 'RemoveHeadersOnCopy']}
  end
end
smoke = project.targets.find { |t| t.name == 'DdlcIOSScriptSmoke' }
if smoke
  ['Replay.cpp', 'DdlcAssets.cpp'].each do |name|
    path = "../games/doki-doki-literature-club/src/#{name}"
    unless smoke.source_build_phase.files.any? { |f| f.file_ref&.path == path }
      ref = project.main_group.new_file(path, 'SOURCE_ROOT')
      smoke.source_build_phase.add_file_reference(ref)
    end
  end
end
project.save
