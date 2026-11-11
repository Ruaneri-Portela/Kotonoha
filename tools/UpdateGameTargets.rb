#!/usr/bin/env ruby
# Run with: ruby tools/UpdateGameTargets.rb
require 'pathname'

root = Pathname.new(__dir__).parent

[
  root.join('buildsystems/xcode/macos/KotonohaDesktop.xcodeproj/project.pbxproj'),
  root.join('buildsystems/xcode/modules/KotonohaModules.xcodeproj/project.pbxproj')
].each do |pbxproj_path|
  if File.exist?(pbxproj_path)
    content = File.read(pbxproj_path)
    content.gsub!(/MACOSX_DEPLOYMENT_TARGET = 10\.\d+;/, 'MACOSX_DEPLOYMENT_TARGET = 12.0;')
    content.gsub!(/MACOSX_DEPLOYMENT_TARGET = 10\.\d+/, 'MACOSX_DEPLOYMENT_TARGET = 12.0')

    if pbxproj_path.to_s.include?('modules')
      content.gsub!('../../lib/', '../../../../lib/')
      content.gsub!(/\t\t\t\tCE1893F83095457F00B0DF80 \/\* KotonohaEngine\.framework \*\/,\n/, '')
      content.gsub!(/\t\t\t\tCE1893F63095457700B0DF80 \/\* KotonohaImGui\.framework \*\/,\n/, '')
      content.sub!('MOD0060 /* Debug */ = {', 'MOD0062 /* Debug */ = {')
      content.sub!('MOD0060 /* Debug */', 'MOD0062 /* Debug */')
    end

    File.write(pbxproj_path, content)
    puts "Updated #{pbxproj_path}"
  end
end
