#include "undo/Commands.h"
#include <QGraphicsScene>
#include <QGraphicsItem>

// -------- AddItemCommand ----------
AddItemCommand::AddItemCommand(QGraphicsScene* scene, QGraphicsItem* item, QUndoCommand* parent)
    : QUndoCommand(parent), m_scene(scene), m_item(item) {
    setText("Add item");
}

void AddItemCommand::undo() { if (m_item && m_scene) m_scene->removeItem(m_item); }
void AddItemCommand::redo() {
    if (m_firstRedo) { m_firstRedo = false; return; }
    if (m_item && m_scene) m_scene->addItem(m_item);
}

// -------- MoveItemsCommand ----------
MoveItemsCommand::MoveItemsCommand(const QVector<QGraphicsItem*>& items,
                                   const QVector<QPointF>& oldPos,
                                   const QVector<QPointF>& newPos,
                                   QUndoCommand* parent)
    : QUndoCommand(parent), m_items(items), m_old(oldPos), m_new(newPos) {
    setText("Move");
}

void MoveItemsCommand::undo() {
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items[i]) m_items[i]->setPos(m_old[i]);
}
void MoveItemsCommand::redo() {
    if (m_firstRedo) { m_firstRedo = false; return; }
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items[i]) m_items[i]->setPos(m_new[i]);
}

// -------- DeleteItemsCommand ----------
DeleteItemsCommand::DeleteItemsCommand(QGraphicsScene* scene,
                                       const QVector<QGraphicsItem*>& items,
                                       QUndoCommand* parent)
    : QUndoCommand(parent), m_scene(scene), m_items(items) {
    setText("Delete");
}

void DeleteItemsCommand::undo() {
    if (!m_scene) return;
    for (auto* it : m_items) if (it) m_scene->addItem(it);
}

void DeleteItemsCommand::redo() {
    if (!m_scene) return;
    for (auto* it : m_items) if (it) m_scene->removeItem(it);
}
