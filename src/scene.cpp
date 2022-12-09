#include "scene.h"

namespace scene {

using Nodetype = Node::Type;

glm::mat4 Translation::transform() const
{
    return glm::translate(glm::mat4(1.0f), m_translation);
}

glm::mat4 Rotation::transform() const
{
    return glm::toMat4(m_rotation);
}

SceneVisitor::SceneVisitor()
{
    m_matrix_stack.push(glm::mat4(1.f));
}

void SceneVisitor::visit(Node* node)
{
    // TODO convert to iteration
    bool should_pop_matrix = false;

    switch (node->type()) {
    case Nodetype::StaticTransform:
    case Nodetype::Rotation:
    case Nodetype::Translation:
        should_pop_matrix = true;
        m_matrix_stack.push(m_matrix_stack.top() * node->transform());
        break;
    case Nodetype::Geometry:
        visitGeometry(*reinterpret_cast<Geometry*>(node));
        break;
    default:
        break;
    }

    for (Node* child : node->children()) {
        visit(child);
    }

    if (should_pop_matrix)
        m_matrix_stack.pop();
}

}
