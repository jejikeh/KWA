#include "core/io/markdown.h"
#include "core/error.h"

namespace kw {

template <typename T>
T& MarkdownNode::as() {
    T* result = dynamic_cast<T*>(this);

    KW_ERROR(
        result != nullptr,
        "Unexpected markdown node type."
    );

    return *result;
}

template NumberNode& MarkdownNode::as<NumberNode>();
template StringNode& MarkdownNode::as<StringNode>();
template BooleanNode& MarkdownNode::as<BooleanNode>();
template ObjectNode& MarkdownNode::as<ObjectNode>();
template ArrayNode& MarkdownNode::as<ArrayNode>();

template <typename T>
const T& MarkdownNode::as() const {
    const T* result = dynamic_cast<const T*>(this);

    KW_ERROR(
        result != nullptr,
        "Unexpected markdown node type."
    );

    return *result;
}

template const NumberNode& MarkdownNode::as<NumberNode>() const;
template const StringNode& MarkdownNode::as<StringNode>() const;
template const BooleanNode& MarkdownNode::as<BooleanNode>() const;
template const ObjectNode& MarkdownNode::as<ObjectNode>() const;
template const ArrayNode& MarkdownNode::as<ArrayNode>() const;

bool ObjectNode::TransparentLess::operator()(const String& lhs, const String& rhs) const {
    return lhs < rhs;
}

bool ObjectNode::TransparentLess::operator()(const char* lhs, const String& rhs) const {
    return lhs < rhs;
}

bool ObjectNode::TransparentLess::operator()(const String& lhs, const char* rhs) const {
    return lhs < rhs;
}

ObjectNode::ObjectNode(Map<String, UniquePtr<MarkdownNode>, TransparentLess>&& elements)
    : m_elements(std::move(elements))
{
}

MarkdownNode& ObjectNode::operator[](const char* key) const {
    auto it = m_elements.find(key);

    KW_ERROR(
        it != m_elements.end(),
        "Unexpected markdown object key."
    );

    return *it->second;
}

MarkdownNode* ObjectNode::find(const char* key) {
    auto it = m_elements.find(key);
    if (it != m_elements.end()) {
        return it->second.get();
    } else {
        return nullptr;
    }
}

size_t ObjectNode::get_size() const {
    return m_elements.size();
}

ObjectNode::iterator ObjectNode::begin() const {
    return m_elements.begin();
}

ObjectNode::iterator ObjectNode::end() const {
    return m_elements.end();
}

ArrayNode::ArrayNode(Vector<UniquePtr<MarkdownNode>>&& elements)
    : m_elements(std::move(elements))
{
}

MarkdownNode& ArrayNode::operator[](size_t index) const {
    KW_ERROR(
        index < m_elements.size(),
        "Unexpected markdown array index."
    );

    return *m_elements[index];
}

size_t ArrayNode::get_size() const {
    return m_elements.size();
}

ArrayNode::iterator ArrayNode::begin() const {
    return m_elements.begin();
}

ArrayNode::iterator ArrayNode::end() const {
    return m_elements.end();
}

} // namespace kw
