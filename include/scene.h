#pragma once
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <list>
#include <stack>
#include <vkw/Render.h>

namespace scene {

class Mesh {
public:
    virtual void initialize_buffers(vkw::CommandBuffer& cmd) = 0;
    virtual void cleanup_initialize_buffers() = 0;
    virtual void draw(vkw::CommandBuffer& cmd) const = 0;
};

class Material {
protected:
    vkw::DescriptorSet m_descriptor_set;

public:
    Material(vkw::DescriptorSet&& d)
        : m_descriptor_set(std::move(d))
    {
    }
    virtual void initialize_buffers(vkw::CommandBuffer& cmd) = 0;
    virtual void cleanup_initialize_buffers() = 0;
    virtual const vkw::DescriptorSet& descriptor_set() const { return m_descriptor_set; }
};

class Node {
protected:
    Node* m_parent;
    std::list<Node*> m_children;

public:
    enum class Type {
        Group,
        Geometry,
        StaticTransform,
        Rotation,
    };

    Node(Node* parent)
        : m_parent(parent)
    {
        if (parent)
            parent->add_child(this);
    }

    virtual Type type() const noexcept = 0;
    virtual glm::mat4 transform() const { return glm::mat4(); }
    const std::list<Node*>& children() const { return m_children; }

    void add_child(Node* n) { m_children.push_back(n); }
    void remove_child(Node* n) { m_children.remove(n); }
};

class Group : public Node {
public:
    Group(Node* parent)
        : Node(parent)
    {
    }
    virtual Type type() const noexcept { return Type::Group; }
};

class Geometry : public Node {
private:
    Mesh& m_mesh;
    Material& m_material;

public:
    Geometry(Node* parent, Mesh& mesh, Material& material)
        : Node(parent)
        , m_mesh(mesh)
        , m_material(material)
    {
    }
    virtual Type type() const noexcept { return Type::Geometry; }

    const Mesh& mesh() const { return m_mesh; }
    const Material& material() const { return m_material; }
};

class StaticTransform : public Node {
private:
    const glm::mat4 m_transform;

public:
    StaticTransform(Node* parent, const glm::mat4& transform)
        : Node(parent)
        , m_transform(transform)
    {
    }
    virtual Type type() const noexcept { return Type::StaticTransform; }
    virtual glm::mat4 transform() const { return m_transform; }
};

class Rotation : public Node {
private:
    glm::quat m_rotation;

public:
    Rotation(Node* parent, const glm::quat& rotation = { 1.f, 0.f, 0.f, 0.f })
        : Node(parent)
        , m_rotation(rotation)
    {
    }
    virtual Type type() const noexcept { return Type::Rotation; }
    virtual glm::mat4 transform() const;

    void set_rotation(const glm::quat& new_rotation)
    {
        m_rotation = new_rotation;
    }
};

class Scene {
    Group m_root;

public:
    Scene()
        : m_root(nullptr)
    {
    }

    inline Node* root() { return &m_root; }
};

class SceneVisitor {
    std::stack<glm::mat4> m_matrix_stack;

public:
    SceneVisitor();

    void visit(Scene& scene) { visit(scene.root()); }
    void visit(Node* node);
    inline const glm::mat4& current_matrix() const { return m_matrix_stack.top(); }

    virtual void visitGeometry(Geometry& geometry) = 0;
};

}

using Scene = scene::Scene;
