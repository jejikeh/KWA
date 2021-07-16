#pragma once

#include "core/containers/map.h"
#include "core/containers/string.h"
#include "core/containers/unique_ptr.h"
#include "core/containers/vector.h"

namespace kw {

class MarkdownNode {
public:
    MarkdownNode() = default;
    MarkdownNode(const MarkdownNode& other) = delete;
    MarkdownNode(MarkdownNode&& other) = default;
    virtual ~MarkdownNode() = default;
    MarkdownNode& operator=(const MarkdownNode& other) = delete;
    MarkdownNode& operator=(MarkdownNode&& other) = default;

    // Throws error for other node types.
    template <typename T>
    T& as();
    
    // Throws error for other node types.
    template <typename T>
    const T& as() const;

    // Return nullptr for other node types.
    template <typename T>
    T* is() {
        return dynamic_cast<T*>(this);
    }
    
    // Return nullptr for other node types.
    template <typename T>
    const T* is() const {
        return dynamic_cast<const T*>(this);
    }
};

class NumberNode : public MarkdownNode {
public:
    explicit NumberNode(double value)
        : m_value(value)
    {
    }
        
    double get_value() const {
        return m_value;
    }

private:
    double m_value;
};

class StringNode : public MarkdownNode {
public:
    explicit StringNode(String&& value)
        : m_value(std::move(value))
    {
    }
        
    const String& get_value() const {
        return m_value;
    }

private:
    String m_value;
};

class BooleanNode : public MarkdownNode {
public:
    explicit BooleanNode(bool value)
        : m_value(value)
    {
    }
        
    bool get_value() const {
        return m_value;
    }

private:
    bool m_value;
};

class ObjectNode : public MarkdownNode {
public:
    struct TransparentLess {
        using is_transparent = void;

        bool operator()(const String& lhs, const String& rhs) const;
        bool operator()(const char* lhs, const String& rhs) const;
        bool operator()(const String& lhs, const char* rhs) const;
    };

    using iterator = Map<String, UniquePtr<MarkdownNode>, TransparentLess>::const_iterator;

    explicit ObjectNode(Map<String, UniquePtr<MarkdownNode>, TransparentLess>&& elements);

    // Throws when key doesn't exist.
    MarkdownNode& operator[](const char* key) const;

    // Returns nullptr when key doesn't exist.
    MarkdownNode* find(const char* key);

    size_t get_size() const;

    iterator begin() const;
    iterator end() const;

private:
    Map<String, UniquePtr<MarkdownNode>, TransparentLess> m_elements;
};

class ArrayNode : public MarkdownNode {
public:
    using iterator = Vector<UniquePtr<MarkdownNode>>::const_iterator;

    explicit ArrayNode(Vector<UniquePtr<MarkdownNode>>&& elements);

    // Throws when out of bounds.
    MarkdownNode& operator[](size_t index) const;
    size_t get_size() const;

    iterator begin() const;
    iterator end() const;

private:
    Vector<UniquePtr<MarkdownNode>> m_elements;
};

} // namespace kw
