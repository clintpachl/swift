module Swift

  # Adapter.
  #
  # @abstract
  # @see      Swift::DB See Swift::DB for concrete adapters.
  class Adapter
    attr_reader :options

    # Select by id(s).
    #
    # @example Single key.
    #   Swift.db.get(User, id: 12)
    # @example Complex primary key.
    #   Swift.db.get(UserAddress, user_id: 12, address_id: 15)
    #
    # @param  [Swift::Scheme] scheme Concrete scheme subclass to load.
    # @param  [Hash]          keys   Hash of id(s) <tt>{id_name: value}</tt>.
    # @return [Swift::Scheme, nil]
    # @see    Swift::Scheme.get
    #--
    # NOTE: Not significantly shorter than Scheme.db.first(User, 'id = ?', 12)
    def get scheme, keys
      resource = scheme.new(keys)
      prepare_get(scheme).execute(*resource.tuple.values_at(*scheme.header.keys)).first
    end

    # Create one or more.
    #
    # @example Scheme.
    #   user = User.new(name: 'Apply Arthurton', age: 32)
    #   Swift.db.create(User, user)
    #   #=> Swift::Scheme
    # @example Coerce hash to scheme.
    #   Swif.db.create(User, name: 'Apple Arthurton', age: 32)
    #   #=> Swift::Scheme
    # @example Multiple resources.
    #   apple = User.new(name: 'Apple Arthurton', age: 32)
    #   benny = User.new(name: 'Benny Arthurton', age: 30)
    #   Swift.db.create(User, [apple, benny])
    #   #=> Array<Swift::Scheme>
    # @example Coerce multiple resources.
    #   Swift.db.create(User, [{name: 'Apple Arthurton', age: 32}, {name: 'Benny Arthurton', age: 30}])
    #   #=> Array<Swift::Scheme>
    #
    # @param  [Swift::Scheme]                                   scheme    Concrete scheme subclass to load.
    # @param  [Swift::Scheme, Hash, Array<Swift::Scheme, Hash>] resources The resources to be saved.
    # @return [Swift::Scheme, Array<Swift::Scheme>]
    # @note   Hashes will be coerced into a Swift::Scheme resource via Swift::Scheme#new
    # @note   Passing a scalar will result in a scalar.
    # @see    Swift::Scheme.create
    def create scheme, resources
      statement = prepare_create(scheme)
      result    = [resources].flatten.map do |resource|
        resource = scheme.new(resource) unless resource.kind_of?(scheme)
        result   = statement.execute(*resource.tuple.values_at(*scheme.header.insertable))
        resource.tuple[scheme.header.serial] = result.insert_id if scheme.header.serial
        resource
      end
      resources.kind_of?(Array) ? result : result.first
    end

    # Update one or more.
    #
    # @example Scheme.
    #   user      = Swift.db.create(User, name: 'Apply Arthurton', age: 32)
    #   user.name = 'Arthur Appleton'
    #   Swift.db.update(User, user)
    #   #=> Swift::Scheme
    # @example Coerce hash to scheme.
    #   user      = Swift.db.create(User, name: 'Apply Arthurton', age: 32)
    #   user.name = 'Arthur Appleton'
    #   Swif.db.update(User, user.tuple)
    #   #=> Swift::Scheme
    # @example Multiple resources.
    #   apple = Swift.db.create(User, name: 'Apple Arthurton', age: 32)
    #   benny = Swift.db.create(User, name: 'Benny Arthurton', age: 30)
    #   Swift.db.update(User, [apple, benny])
    #   #=> Array<Swift::Scheme>
    # @example Coerce multiple resources.
    #   apple = Swift.db.create(User, name: 'Apple Arthurton', age: 32)
    #   benny = Swift.db.create(User, name: 'Benny Arthurton', age: 30)
    #   Swift.db.update(User, [apple.tuple, benny.tuple])
    #   #=> Array<Swift::Scheme>
    #
    # @param  [Swift::Scheme]                                   scheme    Concrete scheme subclass to load.
    # @param  [Swift::Scheme, Hash, Array<Swift::Scheme, Hash>] resources The resources to be updated.
    # @return [Swift::Scheme, Swift::Result]
    # @note   Hashes will be coerced into a Swift::Scheme resource via Swift::Scheme#new
    # @note   Passing a scalar will result in a scalar.
    # @see    Swift::Scheme#update
    def update scheme, resources
      statement = prepare_update(scheme)
      result    = [resources].flatten.map do |resource|
        resource = scheme.new(resource) unless resource.kind_of?(scheme)
        keys     = resource.tuple.values_at(*scheme.header.keys)

        # TODO: Name the key field(s) missing.
        raise ArgumentError, "#{scheme} resource has incomplete key: #{resource.inspect}" \
          unless keys.select(&:nil?).empty?

        statement.execute(*resource.tuple.values_at(*scheme.header.updatable), *keys)
        resource
      end
      resources.kind_of?(Array) ? result : result.first
    end

    # Delete one or more.
    #
    # @example Scheme.
    #   user      = Swift.db.create(User, name: 'Apply Arthurton', age: 32)
    #   user.name = 'Arthur Appleton'
    #   Swift.db.delete(User, user)
    # @example Coerce hash to scheme.
    #   user      = Swift.db.create(User, name: 'Apply Arthurton', age: 32)
    #   user.name = 'Arthur Appleton'
    #   Swif.db.delete(User, user.tuple)
    # @example Multiple resources.
    #   apple = Swift.db.create(User, name: 'Apple Arthurton', age: 32)
    #   benny = Swift.db.create(User, name: 'Benny Arthurton', age: 30)
    #   Swift.db.delete(User, [apple, benny])
    # @example Coerce multiple resources.
    #   apple = Swift.db.create(User, name: 'Apple Arthurton', age: 32)
    #   benny = Swift.db.create(User, name: 'Benny Arthurton', age: 30)
    #   Swift.db.delete(User, [apple.tuple, benny.tuple])
    #
    # @param  [Swift::Scheme]                                   scheme    Concrete scheme subclass to load.
    # @param  [Swift::Scheme, Hash, Array<Swift::Scheme, Hash>] resources The resources to be deleteed.
    # @return [Swift::Scheme, Array<Swift::Scheme>]
    # @note   Hashes will be coerced into a Swift::Scheme resource via Swift::Scheme#new
    # @note   Passing a scalar will result in a scalar.
    # @see    Swift::Scheme#delete
    def delete scheme, resources
      statement = prepare_delete(scheme)
      result    = [resources].flatten.map do |resource|
        resource = scheme.new(resource) unless resource.kind_of?(scheme)
        keys     = resource.tuple.values_at(*scheme.header.keys)

        # TODO: Name the key field(s) missing.
        raise ArgumentError, "#{scheme} resource has incomplete key: #{resource.inspect}" \
          unless keys.select(&:nil?).empty?

        if result = statement.execute(*keys)
          resource.freeze
        end
        result
      end
      resources.kind_of?(Array) ? result : result.first
    end

    protected
      def prepare_get scheme
        raise NotImplementedError
      end

      def prepare_create scheme
        raise NotImplementedError
      end

      def prepare_update scheme
        raise NotImplementedError
      end

      def prepare_delete scheme
        raise NotImplementedError
      end

  end # Adapter
end # Swift
