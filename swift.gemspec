# Generated by jeweler
# DO NOT EDIT THIS FILE DIRECTLY
# Instead, edit Jeweler::Tasks in Rakefile, and run 'rake gemspec'
# -*- encoding: utf-8 -*-

Gem::Specification.new do |s|
  s.name = %q{swift}
  s.version = "0.14.0"

  s.required_rubygems_version = Gem::Requirement.new(">= 0") if s.respond_to? :required_rubygems_version=
  s.authors = ["Shane Hanna", "Bharanee 'Barney' Rathna"]
  s.date = %q{2012-03-22}
  s.description = %q{A rational rudimentary database abstraction.}
  s.email = ["shane.hanna@gmail.com", "deepfryed@gmail.com"]
  s.extensions = ["ext/extconf.rb"]
  s.extra_rdoc_files = [
    "LICENSE",
    "README.md"
  ]
  s.files = [
    "API.rdoc",
    "LICENSE",
    "README.md",
    "Rakefile",
    "VERSION",
    "ext/adapter.cc",
    "ext/adapter.h",
    "ext/adapter_io.cc",
    "ext/adapter_io.h",
    "ext/attribute.cc",
    "ext/attribute.h",
    "ext/datetime.cc",
    "ext/datetime.h",
    "ext/extconf.rb",
    "ext/query.cc",
    "ext/query.h",
    "ext/result.cc",
    "ext/result.h",
    "ext/statement.cc",
    "ext/statement.h",
    "ext/swift.cc",
    "ext/swift.h",
    "lib/swift.rb",
    "lib/swift/adapter.rb",
    "lib/swift/adapter/sql.rb",
    "lib/swift/attribute.rb",
    "lib/swift/db.rb",
    "lib/swift/header.rb",
    "lib/swift/identity_map.rb",
    "lib/swift/migrations.rb",
    "lib/swift/scheme.rb",
    "lib/swift/type.rb",
    "lib/swift/validations.rb",
    "swift.gemspec",
    "test/helper.rb",
    "test/house-explode.jpg",
    "test/minitest_teardown_hack.rb",
    "test/test_adapter.rb",
    "test/test_async.rb",
    "test/test_datetime_parser.rb",
    "test/test_encoding.rb",
    "test/test_error.rb",
    "test/test_identity_map.rb",
    "test/test_io.rb",
    "test/test_scheme.rb",
    "test/test_swift.rb",
    "test/test_timestamps.rb",
    "test/test_transactions.rb",
    "test/test_types.rb",
    "test/test_validations.rb"
  ]
  s.homepage = %q{http://github.com/shanna/swift}
  s.require_paths = ["lib"]
  s.rubygems_version = %q{1.3.7}
  s.summary = %q{A rational rudimentary database abstraction.}

  if s.respond_to? :specification_version then
    current_version = Gem::Specification::CURRENT_SPECIFICATION_VERSION
    s.specification_version = 3

    if Gem::Version.new(Gem::VERSION) >= Gem::Version.new('1.2.0') then
      s.add_development_dependency(%q<minitest>, [">= 1.7.0"])
    else
      s.add_dependency(%q<minitest>, [">= 1.7.0"])
    end
  else
    s.add_dependency(%q<minitest>, [">= 1.7.0"])
  end
end

