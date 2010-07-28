#!/usr/bin/env ruby
require_relative '../lib/swift'
require 'etc'
require 'pp'

class User < Swift.resource do
    store    :users
    attribute :id,       Integer, serial: true, key: true
    attribute :name,     String
    attribute :email,    String
    attribute :active,   Boolean
    attribute :created,  Time,   default: proc { Time.now }
    attribute :optional, String, default: 'woot'
  end
end # User

Swift.setup user: Etc.getlogin, db: 'swift', driver: ARGV[0] || 'postgresql'
Swift.trace true

Swift.db do
  puts '-- migrate! --'
  User.migrate!

  puts '', '-- create --'
  User.create name: 'Apple Arthurton', email: 'apple@arthurton.local'
  User.create name: 'Benny Arthurton', email: 'benny@arthurton.local'

  puts '', '-- all --'
  pp User.all(':name like ? limit 1 offset 1', '%Arthurton').first

  puts '', '-- get --'
  pp user = User.get(id: 2)
  pp user = User.get(id: 2)

  puts '', '-- update --'
  user.update(name: 'Jimmy Arthurton')

  puts '', '-- destroy --'
  user.destroy
end
