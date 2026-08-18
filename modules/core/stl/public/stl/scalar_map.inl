/// \file
/// \brief Scalar SwissTable — the linear-probe fallback used when no SIMD/SWAR
///        group backend is selected (i.e. \c MLW_LINIAR_MAP_PROBE, or an
///        architecture with no group scan).
///
/// This is the same open-addressing hash map as the SIMD \ref core::Map, with
/// one difference: the control array is scanned **one byte at a time** instead
/// of a group at a time. Everything else — the 7/8 load factor, tombstone
/// reuse, the "downgrade to EMPTY when the next slot is empty" erase — is
/// identical, which is what makes it a drop-in oracle for the SIMD version.
///
/// \par Capacity convention
///   \c capacity is the probe bitmask, always <tt>2^k - 1</tt> (all ones), so
///   <tt>pos & capacity</tt> is the wrap-around modulo. Unlike the SIMD map,
///   there is **no sentinel and no cloned tail**: the byte scan wraps purely
///   through the mask, so every masked position <tt>0..capacity</tt> is a real
///   slot. The number of real slots is therefore <tt>capacity + 1</tt>
///   (a power of two); \ref slotCount returns it.
///
/// \par Single-block allocation
///   \c ctrl and \c data live in one allocation: <tt>[ctrl bytes][pad][slots]</tt>.
///   \c ctrl is the block base (also the pointer passed to free); \c data
///   starts at \ref blockLayout 's \c slotOffset, rounded up to
///   <tt>alignof(Entry)</tt>. The whole block is aligned to <tt>alignof(Entry)</tt>.
///   Control comes first purely for symmetry with the SIMD map — the scalar map
///   has no tail to protect, so either order would be safe here.
///
/// \note Not reference-stable. Any \ref put that grows the table relocates every
///       entry, invalidating references returned by \ref get / \ref put and any
///       stored \c Entry*. Hold a copy of the value, not a reference, across
///       inserts. (Same contract as the SIMD map; see \ref core::Map.)
/// \note Not copyable (use \ref clone); movable.

template <HashKey K, HashStorable V>
class ScalarMap
{
	using ctrl_t = swiss::ctrl_t;

public:
	struct Entry
	{
		K key;
		V value;
	};

private:
	Entry *data = nullptr;
	ctrl_t *ctrl = nullptr; // control bytes; also the block base pointer
	isize capacity = 0;		// probe bitmask (2^k - 1), or 0 when unallocated
	isize size = 0;			// live elements
	isize growth_left = 0;	// inserts-into-empty remaining before we must grow
	AnonymousAllocator allocator;

	// Real slots = capacity + 1 (capacity is the all-ones bitmask; no sentinel).
	isize slotCount() const { return capacity == 0 ? 0 : capacity + 1; }
	// 7/8 load factor, counting tombstones as occupied.
	static isize loadLimit(isize slots) { return slots - slots / 8; }

	// One block: [ctrl: slotCount bytes][pad to alignof(Entry)][slots: slotCount].
	// Returns total bytes; writes the slot region's offset to slotOffsetOut.
	static usize blockLayout(isize cap, usize &slotOffsetOut)
	{
		const usize slots = static_cast<usize>(cap) + 1; // scalar: cap+1 real slots
		const usize ctrlBytes = slots;					 // one control byte per slot, no tail
		const usize a = alignof(Entry);
		const usize slotOffset = (ctrlBytes + a - 1) & ~(a - 1);
		slotOffsetOut = slotOffset;
		return slotOffset + slots * sizeof(Entry);
	}
	static usize blockAlign() { return alignof(Entry); }

	void allocateTables(isize cap)
	{
		usize slotOffset;
		const usize total = blockLayout(cap, slotOffset);
		void *p = allocator.realloc(allocator.ctx, nullptr, 0,
									static_cast<isize>(total), static_cast<isize>(blockAlign()));
		mlw_debug_assert_msg(p != nullptr, "ScalarMap allocation returned nullptr");

		ctrl = static_cast<ctrl_t *>(p);
		data = reinterpret_cast<Entry *>(static_cast<uint8 *>(p) + slotOffset);
		capacity = cap;

		core::mlwMemset(ctrl, static_cast<int>(static_cast<uint8>(swiss::EMPTY)),
						static_cast<usize>(slotCount()));
	}

	void freeBlock(ctrl_t *block, isize cap)
	{
		usize slotOffset;
		const usize total = blockLayout(cap, slotOffset);
		allocator.realloc(allocator.ctx, block, static_cast<isize>(total), 0,
						  static_cast<isize>(blockAlign()));
	}

	Entry *findEntry(usize hash, const K &key) const
	{
		if (capacity == 0)
			return nullptr;
		usize pos = swiss::H1(hash) & static_cast<usize>(capacity);
		const ctrl_t h2 = swiss::H2(hash);
		while (true)
		{
			const ctrl_t c = ctrl[pos];
			if (c == h2 && data[pos].key == key)
				return &data[pos]; // byte filter passed, key verified
			if (c == swiss::EMPTY)
				return nullptr; // chain ended -> not present
			pos = (pos + 1) & static_cast<usize>(capacity);
		}
	}

	// First slot a new key may occupy: empty OR a reusable tombstone.
	usize findFirstNonFull(usize hash) const
	{
		usize pos = swiss::H1(hash) & static_cast<usize>(capacity);
		while (true)
		{
			const ctrl_t c = ctrl[pos];
			if (c == swiss::EMPTY || c == swiss::DELETED)
				return pos;
			pos = (pos + 1) & static_cast<usize>(capacity);
		}
	}

	void rehashAndGrow()
	{
		Entry *oldData = data;
		ctrl_t *oldCtrl = ctrl;
		const isize oldCap = capacity;

		const isize newCap = (capacity == 0) ? 15 : (capacity * 2 + 1); // 15,31,63,...
		allocateTables(newCap);										   // sets ctrl/data/capacity, fills EMPTY
		size = 0;

		if (oldCtrl != nullptr)
		{
			const isize oldSlots = oldCap + 1;
			for (isize i = 0; i < oldSlots; ++i)
			{
				if (!swiss::isFull(oldCtrl[i]))
					continue;
				Entry &e = oldData[i];
				const usize hash = swiss::hashKey(e.key);
				const usize slot = findFirstNonFull(hash);
				::new (&data[slot].key) K(core::move_if_movable(e.key));
				::new (&data[slot].value) V(core::move_if_movable(e.value));
				e.key.~K();
				e.value.~V();
				ctrl[slot] = swiss::H2(hash);
				++size;
			}
			freeBlock(oldCtrl, oldCap);
		}

		// Fresh table has no tombstones, so the budget is simply limit - size.
		growth_left = loadLimit(slotCount()) - size;
	}

	usize prepareInsert(usize hash)
	{
		usize target;
		if (capacity == 0)
		{
			rehashAndGrow();
			target = findFirstNonFull(hash);
		}
		else
		{
			target = findFirstNonFull(hash);
			// Out of budget AND we'd consume a fresh empty (not reuse a
			// tombstone) -> grow. Reusing a tombstone doesn't raise the load.
			if (growth_left == 0 && !swiss::isDeleted(ctrl[target]))
			{
				rehashAndGrow();
				target = findFirstNonFull(hash);
			}
		}
		++size;
		if (swiss::isEmpty(ctrl[target]))
			--growth_left; // consumed an empty; tombstone reuse doesn't
		ctrl[target] = swiss::H2(hash);
		return target;
	}

public:
	ScalarMap() : allocator(core::default_allocator()) {}
	explicit ScalarMap(AnonymousAllocator alloc) : allocator(alloc) {}

	ScalarMap(const ScalarMap &) = delete;
	ScalarMap &operator=(const ScalarMap &) = delete;

	ScalarMap(ScalarMap &&o)
		: data(o.data), ctrl(o.ctrl), capacity(o.capacity), size(o.size),
		  growth_left(o.growth_left), allocator(o.allocator)
	{
		o.data = nullptr;
		o.ctrl = nullptr;
		o.capacity = 0;
		o.size = 0;
		o.growth_left = 0;
	}
	ScalarMap &operator=(ScalarMap &&o) noexcept
	{
		if (this != &o)
		{
			deinit();
			::new (this) ScalarMap(core::move(o));
		}
		return *this;
	}

	~ScalarMap() { deinit(); }

	/// \brief Deep copy. Available only when both K and V are copyable.
	ScalarMap clone() const
		requires(is_copy_constructible_v<K> && is_copy_constructible_v<V>)
	{
		ScalarMap ret{allocator};
		if (capacity == 0)
			return ret;

		usize slotOffset;
		const usize total = blockLayout(capacity, slotOffset);
		void *p = allocator.realloc(allocator.ctx, nullptr, 0,
									static_cast<isize>(total), static_cast<isize>(blockAlign()));
		mlw_debug_assert_msg(p != nullptr, "ScalarMap::clone allocation returned nullptr");

		ret.ctrl = static_cast<ctrl_t *>(p);
		ret.data = reinterpret_cast<Entry *>(static_cast<uint8 *>(p) + slotOffset);
		ret.capacity = capacity;
		ret.size = size;
		ret.growth_left = growth_left;

		const isize S = slotCount();
		core::mlwMemcpy(ret.ctrl, ctrl, static_cast<usize>(S) * sizeof(ctrl_t));

		if constexpr (is_trivially_copyable_v<K> && is_trivially_copyable_v<V>)
		{
			core::mlwMemcpy(ret.data, data, static_cast<usize>(S) * sizeof(Entry));
		}
		else
		{
			for (isize i = 0; i < S; ++i)
				if (swiss::isFull(ctrl[i]))
				{
					::new (&ret.data[i].key) K(static_cast<const K &>(data[i].key));
					::new (&ret.data[i].value) V(static_cast<const V &>(data[i].value));
				}
		}
		return ret;
	}

	// -- capacity ---------------------------------------------------------
	isize len() const { return size; }
	bool isEmpty() const { return size == 0; }

	// -- lookup -----------------------------------------------------------
	Optional<V &> get(const K &key)
	{
		Entry *e = findEntry(swiss::hashKey(key), key);
		if (e == nullptr)
			return nullptr;
		return Optional<V &>{e->value};
	}
	Optional<const V &> get(const K &key) const
	{
		const Entry *e = findEntry(swiss::hashKey(key), key);
		if (e == nullptr)
			return nullptr;
		return Optional<const V &>{e->value};
	}
	bool contains(const K &key) const
	{
		return findEntry(swiss::hashKey(key), key) != nullptr;
	}

	// -- insert -----------------------------------------------------------
	V &put(const K &key, const V &value)
	{
		const usize hash = swiss::hashKey(key);
		if (Entry *e = findEntry(hash, key))
		{
			e->value.~V();
			::new (&e->value) V(value);
			return e->value;
		}
		const usize t = prepareInsert(hash);
		::new (&data[t].key) K(key);
		::new (&data[t].value) V(value);
		return data[t].value;
	}
	V &put(const K &key, V &&value)
	{
		const usize hash = swiss::hashKey(key);
		if (Entry *e = findEntry(hash, key))
		{
			e->value.~V();
			::new (&e->value) V(core::move(value));
			return e->value;
		}
		const usize t = prepareInsert(hash);
		::new (&data[t].key) K(key);
		::new (&data[t].value) V(core::move(value));
		return data[t].value;
	}

	/// \brief Remove \p key if present. \return true if something was removed.
	/// Downgrades the slot to EMPTY when the next slot is already EMPTY (no chain
	/// runs through it), else leaves a DELETED tombstone; resize clears tombstones.
	bool remove(const K &key)
	{
		if (capacity == 0)
			return false;
		const usize hash = swiss::hashKey(key);
		usize pos = swiss::H1(hash) & static_cast<usize>(capacity);
		const ctrl_t h2 = swiss::H2(hash);
		while (true)
		{
			const ctrl_t c = ctrl[pos];
			if (c == h2 && data[pos].key == key)
			{
				data[pos].key.~K();
				data[pos].value.~V();
				if (ctrl[(pos + 1) & static_cast<usize>(capacity)] == swiss::EMPTY)
				{
					ctrl[pos] = swiss::EMPTY;
					++growth_left; // reclaimed a probe-terminating slot
				}
				else
				{
					ctrl[pos] = swiss::DELETED; // tombstone still occupies
				}
				--size;
				return true;
			}
			if (c == swiss::EMPTY)
				return false;
			pos = (pos + 1) & static_cast<usize>(capacity);
		}
	}

	/// \brief Destroy all entries but keep the allocated capacity.
	void clear()
	{
		if (capacity == 0)
			return;
		const isize S = slotCount();
		for (isize i = 0; i < S; ++i)
			if (swiss::isFull(ctrl[i]))
			{
				data[i].key.~K();
				data[i].value.~V();
			}
		core::mlwMemset(ctrl, static_cast<int>(static_cast<uint8>(swiss::EMPTY)), static_cast<usize>(S));
		size = 0;
		growth_left = loadLimit(S);
	}

	/// \brief Destroy all entries and release storage. Idempotent.
	void deinit()
	{
		if (capacity != 0)
		{
			const isize S = slotCount();
			for (isize i = 0; i < S; ++i)
				if (swiss::isFull(ctrl[i]))
				{
					data[i].key.~K();
					data[i].value.~V();
				}
			freeBlock(ctrl, capacity);
		}
		data = nullptr;
		ctrl = nullptr;
		capacity = 0;
		size = 0;
		growth_left = 0;
	}

	// -- iteration (yields Entry& for full slots only) --------------------
	template <typename ScalarMapPtr, typename EntryRef>
	struct IteratorT
	{
		ScalarMapPtr map;
		isize idx;
		void skip()
		{
			const isize S = map->slotCount();
			while (idx < S && !swiss::isFull(map->ctrl[idx]))
				++idx;
		}
		EntryRef operator*() const { return map->data[idx]; }
		bool operator!=(const IteratorT &o) const { return idx != o.idx; }
		IteratorT &operator++()
		{
			++idx;
			skip();
			return *this;
		}
	};
	using Iterator = IteratorT<ScalarMap *, Entry &>;
	using ConstIterator = IteratorT<const ScalarMap *, const Entry &>;

	Iterator begin()
	{
		Iterator it{this, 0};
		it.skip();
		return it;
	}
	Iterator end() { return Iterator{this, slotCount()}; }
	ConstIterator begin() const
	{
		ConstIterator it{this, 0};
		it.skip();
		return it;
	}
	ConstIterator end() const { return ConstIterator{this, slotCount()}; }
};
