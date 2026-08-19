#pragma once

#include <core/memory/anonymous_allocator.h>
#include <core/compilers.h>
#include <core/traits.h>
#include <core/macro.h>
#include <core/optional.h>

/// \file
/// \brief Doubly-linked list implementation.

namespace core
{

    /// \brief Constrains what may be stored in a \ref List.
    ///
    /// A valid element is a non-reference, non-cv-qualified, destructible type
    /// that can be either copied or moved into storage.
    template <typename T>
    concept ListElement =
        !is_reference_v<T> && is_destructible_v<T> &&
        (is_copy_constructible_v<T> || is_move_constructible_v<T>) &&
        // no volatiles or const for now. when needed change
        is_same_v<T, remove_cv_t<T>>;

    /// \ingroup formattable
    /// \brief A doubly-linked list of `T`.
    ///
    /// Stores elements in a doubly-linked structure with proper head/tail pointers.
    /// The head points to the first element and tail points to the last element,
    /// making empty list handling simpler. All operations are performed with
    /// O(1) complexity for insertions/deletions at the front/back, and O(n)
    /// for operations at arbitrary positions.
    ///
    /// \note Not copyable. Use \ref clone for an explicit deep copy, or move.
    /// \note Sizes and indices are signed (\ref isize).
    /// \warning On allocation failure the operations panic; only
    ///          \ref tryReserve reports failure as a value.
    /// \tparam T Element type; see \ref ListElement.
    template <ListElement T>
    class List
    {
    private:
        struct Node
        {
            T data;
            Node *next;
            Node *prev;

            /// \brief Construct a node with the given arguments forwarded to T's constructor.
            template <typename... Args>
            Node(Args &&...args) : data(core::forward<Args>(args)...), next(nullptr), prev(nullptr) {}
        };

        Node *head;   // First element in the list
        Node *tail;   // Last element in the list
        isize length; // Number of elements in the list
        const AnonymousAllocator* allocator;

        /// \brief Create a new node with the given arguments.
        /// \warning Panics on allocation failure.
        template <typename... Args>
        Node *createNode(Args &&...args)
        {
            void *p = allocator->realloc(allocator, nullptr, 0, sizeof(Node), alignof(Node));
            mlw_debug_assert_msg(p != nullptr, "List::createNode failed to allocate");
            return new (p) Node(core::forward<Args>(args)...);
        }

        /// \brief Destroy a node and free its memory.
        void destroyNode(Node *node)
        {
            node->data.~T();
            allocator->realloc(allocator, node, sizeof(Node), 0, alignof(Node));
        }

        /// \brief Get the node at index \p i.
        /// \pre `0 <= i < length`.
        Node *getNode(isize i) const
        {
            mlw_debug_assert_msg(i >= 0 && i < length, "List::getNode out of bounds");

            isize distance_from_front = i;
            isize distance_from_back = length - 1 - i;

            if (distance_from_front <= distance_from_back)
            {
                Node *current = head;
                for (isize j = 0; j < i; ++j)
                    current = current->next;
                return current;
            }
            else
            {
                Node *current = tail;
                for (isize j = length - 1; j > i; --j)
                    current = current->prev;
                return current;
            }
        }

    public:
        // -- lifetime -----------------------------------------------------

        /// \brief Construct an empty list using \ref default_allocator.
        List() : head(nullptr), tail(nullptr), length(0), allocator(&core::default_allocator()) {}

        /// \brief Construct an empty list that allocates from \p alloc.
        /// \param alloc Allocator captured by value and used for the list's
        ///              lifetime.
        explicit List(const AnonymousAllocator* alloc) : head(nullptr), tail(nullptr), length(0), allocator(alloc) {}

        /// \brief Deleted: lists are not implicitly copyable.
        /// \see clone
        List(const List &) = delete;
        /// \copydoc List(const List&)
        List &operator=(const List &) = delete;

        /// \brief Return a deep copy: a new list with copies of every element.
        ///
        /// The copy uses the same allocator as `*this`. Each element is copy-constructed.
        /// \return A list of the same length with element-wise copies.
        /// \warning Panics on allocation failure.
        /// \note Available only when `T` is copy-constructible.
        List clone() const
            requires is_copy_constructible_v<T>
        {
            List ret{allocator};

            if (length == 0)
                return ret; // Already empty

            Node *current = head;
            for (isize i = 0; i < length; ++i)
            {
                ret.pushBack(current->data);
                current = current->next;
            }

            return ret;
        }

        /// \brief Move-construct, taking ownership of \p other's storage.
        /// \post \p other is left empty and valid.
        List(List &&other) : head(other.head), tail(other.tail), length(other.length), allocator(other.allocator)
        {
            other.head = nullptr;
            other.tail = nullptr;
            other.length = 0;
        };

        /// \brief Move-assign: release current storage, then take \p other's.
        /// \post \p other is left empty and valid. Self-assignment is a no-op.
        List &operator=(List &&other)
        {
            if (this != &other)
            {
                deinit();
                new (this) List{move(other)};
            }
            return *this;
        };

        /// \brief Destroy all elements and release the storage.
        ///
        /// Leaves the list valid and empty (head/tail null, length zero), so it is
        /// safe to reuse or destroy afterward. Idempotent.
        void deinit()
        {
            clear();
            head = nullptr;
            tail = nullptr;
            length = 0;
        };

        /// \brief Destroy all elements and free storage.
        ~List() { deinit(); };

        // -- capacity -----------------------------------------------------

        isize len() const { return length; }

        /// \brief True if the list holds no elements.
        MLW_FORCE_INLINE bool isEmpty() const { return length == 0; }

        // -- access -------------------------------------------------------

        /// \brief Element at index \p i, or None if out of range.
        Optional<T &> get(isize i)
        {
            if (i < 0 || i >= length) return nullptr;
            return Optional<T &>{getNode(i)->data};
        }
        /// \copydoc at()
        Optional<const T &> get(isize i) const
        {
            if (i < 0 || i >= length) return nullptr;
            return Optional<const T &>{getNode(i)->data};
        }

        /// \brief The first element, or None if empty.
        Optional<T &> front()
        {
            if (length == 0)
            {
                return nullptr;
            }
            return Optional<T &>{head->data};
        }
        /// \copydoc front()
        Optional<const T &> front() const
        {
            if (length == 0)
            {
                return nullptr;
            }
            return Optional<const T &>{head->data};
        }

        /// \brief The last element, or None if empty.
        Optional<T &> back()
        {
            if (length == 0)
            {
                return nullptr;
            }
            return Optional<T &>{tail->data};
        }
        /// \copydoc back()
        Optional<const T &> back() const
        {
            if (length == 0)
            {
                return nullptr;
            }
            return Optional<const T &>{tail->data};
        }

        // -- modifiers ----------------------------------------------------

        /// \brief Append a copy of \p v to the front of the list.
        /// \note Available only when `T` is copy-constructible.
        void pushFront(const T &v)
            requires is_copy_constructible_v<T>
        {
            Node *newNode = createNode(v);
            insertNodeAtHead(newNode);
        }

        /// \brief Append \p v to the front of the list by moving.
        /// \note Available only when `T` is move-constructible.
        void pushFront(T &&v)
            requires is_move_constructible_v<T>
        {
            Node *newNode = createNode(core::move(v));
            insertNodeAtHead(newNode);
        }

        /// \brief Append a copy of \p v to the back of the list.
        /// \note Available only when `T` is copy-constructible.
        void pushBack(const T &v)
            requires is_copy_constructible_v<T>
        {
            Node *newNode = createNode(v);
            insertNodeAtTail(newNode);
        }

        /// \brief Append \p v to the back of the list by moving.
        /// \note Available only when `T` is move-constructible.
        void pushBack(T &&v)
            requires is_move_constructible_v<T>
        {
            Node *newNode = createNode(core::move(v));
            insertNodeAtTail(newNode);
        }

        /// \brief Remove and return the first element.
        /// \pre `!isEmpty()`. \return The removed element.
        T popFront()
        {
            mlw_debug_assert_msg(!isEmpty(), "List::popFront on empty list");
            Node *node = head;
            T ret{move_if_movable(node->data)};
            removeNode(node);
            destroyNode(node);
            return ret;
        }

        /// \brief Remove and return the last element.
        /// \pre `!isEmpty()`. \return The removed element.
        T popBack()
        {
            mlw_debug_assert_msg(!isEmpty(), "List::popBack on empty list");
            Node *node = tail;
            T ret{move_if_movable(node->data)};
            removeNode(node);
            destroyNode(node);
            return ret;
        }

        /// \brief Construct a new element in place at the front from \p args.
        /// \param args Forwarded to `T`'s constructor.
        /// \return Reference to the newly-constructed element.
        template <typename... Args>
        T &emplaceFront(Args &&...args)
            requires is_constructible_v<T, Args...>
        {
            Node *newNode = createNode(core::forward<Args>(args)...);
            insertNodeAtHead(newNode);
            return newNode->data;
        }

        /// \brief Construct a new element in place at the back from \p args.
        /// \param args Forwarded to `T`'s constructor.
        /// \return Reference to the newly-constructed element.
        template <typename... Args>
        T &emplaceBack(Args &&...args)
            requires is_constructible_v<T, Args...>
        {
            Node *newNode = createNode(core::forward<Args>(args)...);
            insertNodeAtTail(newNode);
            return newNode->data;
        }

        /// \brief Insert a copy of \p v at index \p i.
        /// \pre `0 <= i <= len`.
        /// \note Available only when `T` is copy-constructible.
        void insert(isize i, const T &v)
            requires is_copy_constructible_v<T>
        {
            mlw_debug_assert_msg(i >= 0 && i <= length, "List::insert index out of range");
            if (i == length)
            {
                pushBack(v);
                return;
            }

            Node *newNode = createNode(v);
            Node *target = getNode(i);
            insertNodeBefore(target, newNode);
        }

        /// \brief Insert \p v at index \p i by moving.
        /// \pre `0 <= i <= len`.
        /// \note Available only when `T` is move-constructible.
        void insert(isize i, T &&v)
            requires is_move_constructible_v<T>
        {
            mlw_debug_assert_msg(i >= 0 && i <= length, "List::insert index out of range");
            if (i == length)
            {
                pushBack(core::move(v));
                return;
            }

            Node *newNode = createNode(core::move(v));
            Node *target = getNode(i);
            insertNodeBefore(target, newNode);
        }

        /// \brief Remove the element at index \p i.
        /// \pre `0 <= i < len`.
        /// \return The removed element.
        T remove(isize i)
        {
            mlw_debug_assert_msg(i >= 0 && i < length, "List::remove index out of range");
            Node *node = getNode(i);
            T ret{move_if_movable(node->data)};
            removeNode(node);
            destroyNode(node);
            return ret;
        }

        /// \brief Remove all elements from the list.
        void clear()
        {
            Node *current = head;
            while (current)
            {
                Node *next = current->next;
                destroyNode(current);
                current = next;
            }
            head = tail = nullptr;
            length = 0;
        }

        // -- iteration ----------------------------------------------------

        /// \brief Bidirectional iterator over the list.
        /// \warning Invalidated when the element it points to is removed.
        ///          `--end()` is NOT valid here (see note after the class).
        class ConstIterator; // fwd for the friend line below

        class Iterator
        {
            Node *node;
            friend class List;
            friend class ConstIterator;

        public:
            explicit Iterator(Node *n) : node(n) {}

            T &operator*() const { return node->data; }
            T *operator->() const { return &node->data; }

            Iterator &operator++() { node = node->next; return *this; }
            Iterator operator++(int) { Iterator tmp = *this; node = node->next; return tmp; }
            Iterator &operator--() { node = node->prev; return *this; }
            Iterator operator--(int) { Iterator tmp = *this; node = node->prev; return tmp; }

            bool operator==(const Iterator &o) const { return node == o.node; }
            bool operator!=(const Iterator &o) const { return node != o.node; }
        };

        /// \brief Const counterpart of \ref Iterator. Yields `const T&`.
        class ConstIterator
        {
            const Node *node;
            friend class List;

        public:
            explicit ConstIterator(const Node *n) : node(n) {}
            ConstIterator(const Iterator &it) : node(it.node) {} // Iterator -> ConstIterator

            const T &operator*() const { return node->data; }
            const T *operator->() const { return &node->data; }

            ConstIterator &operator++() { node = node->next; return *this; }
            ConstIterator operator++(int) { ConstIterator tmp = *this; node = node->next; return tmp; }
            ConstIterator &operator--() { node = node->prev; return *this; }
            ConstIterator operator--(int) { ConstIterator tmp = *this; node = node->prev; return tmp; }

            bool operator==(const ConstIterator &o) const { return node == o.node; }
            bool operator!=(const ConstIterator &o) const { return node != o.node; }
        };

        /// \brief Iterator to the first element (equals \ref end when empty).
        Iterator begin() { return Iterator{head}; }
        /// \brief Iterator one past the last element (null sentinel).
        Iterator end() { return Iterator{nullptr}; }

        /// \copydoc begin()
        ConstIterator begin() const { return ConstIterator{head}; }
        /// \copydoc end()
        ConstIterator end() const { return ConstIterator{nullptr}; }

        /// \brief Const iterator to the first element, even on a non-const list.
        ConstIterator cbegin() const { return ConstIterator{head}; }
        /// \brief Const iterator one past the last element.
        ConstIterator cend() const { return ConstIterator{nullptr}; }



        template <FormatBuffer Buffer>
			requires(FormattableValue<T, Buffer>)
		void format(Buffer &buffer) const
		{
			buffer.append(CStr("{"));
            Node* current = head;
			for (isize i = 0; i < length; ++i)
			{
				if (i != 0)
					buffer.append(CStr(", "));
				detail::formatValue(buffer, current->data); // see below re: mlw_write vs formatValue
                current= current->next;
			}
			buffer.append(CStr("}"));
		}

    private:
        /// \brief Insert a new node at the head of the list.
        void insertNodeAtHead(Node *newNode)
        {
            if (isEmpty())
            {
                head = tail = newNode;
                ++length;
            }
            else
            {
                insertNodeBefore(head, newNode);
            }
        }

        /// \brief Insert a new node at the tail of the list.
        void insertNodeAtTail(Node *newNode)
        {
            if (isEmpty())
            {
                head = tail = newNode;
            }
            else
            {
                // Insert after the current tail
                newNode->prev = tail;
                tail->next = newNode;
                tail = newNode;
            }
            ++length;
        }

        /// \brief Insert a new node before an existing target node.
        void insertNodeBefore(Node *target, Node *newNode)
        {

            newNode->next = target;
            newNode->prev = target->prev;

            if (target->prev)
                target->prev->next = newNode;
            else
                head = newNode; // We're inserting before the head

            target->prev = newNode;
            ++length;
        }

        /// \brief Remove a node from the list.
        void removeNode(Node *node)
        {
            if (node->next)
                node->next->prev = node->prev;
            else
                tail = node->prev; // This was the last node

            if (node->prev)
                node->prev->next = node->next;
            else
                head = node->next; // This was the first node

            --length;
        }
    };
}