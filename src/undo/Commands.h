#pragma once
#include <QUndoCommand>
#include <QPointF>
#include <QVector>

class QGraphicsScene;
class QGraphicsItem;

// Add single item
class AddItemCommand : public QUndoCommand {
public:
    AddItemCommand(QGraphicsScene* scene, QGraphicsItem* item, QUndoCommand* parent = nullptr);

    void undo() override; // remove
    void redo() override; // add (no-op first time)

private:
    QGraphicsScene* m_scene;
    QGraphicsItem*  m_item;       // not owned
    bool            m_firstRedo { true };
};

// Move many items (store old/new positions)
class MoveItemsCommand : public QUndoCommand {
public:
    MoveItemsCommand(const QVector<QGraphicsItem*>& items,
                     const QVector<QPointF>& oldPos,
                     const QVector<QPointF>& newPos,
                     QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override; // no-op first time

private:
    QVector<QGraphicsItem*> m_items;
    QVector<QPointF>        m_old;
    QVector<QPointF>        m_new;
    bool                    m_firstRedo { true };
};

// Delete many items (remove on redo, re-add on undo)
class DeleteItemsCommand : public QUndoCommand {
public:
    DeleteItemsCommand(QGraphicsScene* scene,
                       const QVector<QGraphicsItem*>& items,
                       QUndoCommand* parent = nullptr);

    void undo() override; // re-add
    void redo() override; // remove (executes immediately)

private:
    QGraphicsScene*         m_scene;
    QVector<QGraphicsItem*> m_items; // not owned
};
