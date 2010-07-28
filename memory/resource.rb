#!/usr/bin/ruby

require 'etc'
require_relative '../lib/swift'

$driver = ARGV[0] || 'postgresql'

Swift.setup :default, db: 'swift', user: Etc.getlogin, driver: $driver

class SwiftUser < Swift.resource do
    store    :users
    attribute :id,         Integer, serial: true, key: true
    attribute :name,       String
    attribute :email,      String
    attribute :updated_at, Time
  end
end # SwiftUser

rows = (ARGV[1] || 500).to_i
iter = (ARGV[2] ||   5).to_i

50.times do |r|
  puts ""
  puts "-- run #{r} --"
  puts ""

  puts `top -n1 -bp #{$$} | grep #{Etc.getlogin}`

  SwiftUser.migrate!
  rows.times {|n| SwiftUser.create(name: "test #{n}", email: "test@example.com", updated_at: Time.now) }
  iter.times {|n| SwiftUser.all.each{|m| [ m.id, m.name, m.email, m.updated_at ] } }
  iter.times {|n| SwiftUser.all.each{|m| m.update(name: "foo", email: "foo@example.com", updated_at: Time.now) } }

  SwiftUser.migrate!
  n = 0
  Swift.db.write("users", *%w{name email updated_at}) do
    data = n < rows ? "test #{n}\ttest@example.com\t#{Time.now}\n" : nil
    n += 1
    data
  end

  puts `top -n1 -bp #{$$} | grep #{Etc.getlogin}`

  GC.start

  puts `top -n1 -bp #{$$} | grep #{Etc.getlogin}`
end
