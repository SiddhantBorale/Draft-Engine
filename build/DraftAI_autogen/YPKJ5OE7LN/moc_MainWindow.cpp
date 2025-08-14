/****************************************************************************
** Meta object code from reading C++ file 'MainWindow.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/ui/MainWindow.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'MainWindow.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.9.1. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN10MainWindowE_t {};
} // unnamed namespace

template <> constexpr inline auto MainWindow::qt_create_metaobjectdata<qt_meta_tag_ZN10MainWindowE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "MainWindow",
        "applyCornerRadius",
        "",
        "applyLineBend",
        "promptForProjectUnits",
        "setScaleInteractive",
        "setDimPrecision",
        "p",
        "changeFillPattern",
        "idx",
        "chooseColor",
        "chooseFillColor",
        "changeLineWidth",
        "w",
        "toggleGrid",
        "newScene",
        "openJson",
        "saveJson",
        "importSvg",
        "exportSvg",
        "runBluePrintAI",
        "onVectoriseFinished",
        "changeCornerRadius",
        "r",
        "zoomIn",
        "zoomOut",
        "zoomReset",
        "zoomToFit",
        "undo",
        "redo",
        "addLayer",
        "removeSelectedLayer",
        "setCurrentLayerFromTree",
        "QTreeWidgetItem*",
        "it",
        "prev",
        "layerItemChanged",
        "column",
        "refineVector",
        "refineOverlapsLight",
        "openAutoRoomsDialog"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'applyCornerRadius'
        QtMocHelpers::SlotData<void()>(1, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'applyLineBend'
        QtMocHelpers::SlotData<void()>(3, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'promptForProjectUnits'
        QtMocHelpers::SlotData<void()>(4, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'setScaleInteractive'
        QtMocHelpers::SlotData<void()>(5, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'setDimPrecision'
        QtMocHelpers::SlotData<void(int)>(6, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 7 },
        }}),
        // Slot 'changeFillPattern'
        QtMocHelpers::SlotData<void(int)>(8, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 9 },
        }}),
        // Slot 'chooseColor'
        QtMocHelpers::SlotData<void()>(10, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'chooseFillColor'
        QtMocHelpers::SlotData<void()>(11, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'changeLineWidth'
        QtMocHelpers::SlotData<void(double)>(12, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Double, 13 },
        }}),
        // Slot 'toggleGrid'
        QtMocHelpers::SlotData<void()>(14, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'newScene'
        QtMocHelpers::SlotData<void()>(15, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'openJson'
        QtMocHelpers::SlotData<void()>(16, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'saveJson'
        QtMocHelpers::SlotData<void()>(17, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'importSvg'
        QtMocHelpers::SlotData<void()>(18, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'exportSvg'
        QtMocHelpers::SlotData<void()>(19, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'runBluePrintAI'
        QtMocHelpers::SlotData<void()>(20, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onVectoriseFinished'
        QtMocHelpers::SlotData<void()>(21, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'changeCornerRadius'
        QtMocHelpers::SlotData<void(double)>(22, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Double, 23 },
        }}),
        // Slot 'zoomIn'
        QtMocHelpers::SlotData<void()>(24, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'zoomOut'
        QtMocHelpers::SlotData<void()>(25, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'zoomReset'
        QtMocHelpers::SlotData<void()>(26, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'zoomToFit'
        QtMocHelpers::SlotData<void()>(27, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'undo'
        QtMocHelpers::SlotData<void()>(28, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'redo'
        QtMocHelpers::SlotData<void()>(29, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'addLayer'
        QtMocHelpers::SlotData<void()>(30, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'removeSelectedLayer'
        QtMocHelpers::SlotData<void()>(31, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'setCurrentLayerFromTree'
        QtMocHelpers::SlotData<void(QTreeWidgetItem *, QTreeWidgetItem *)>(32, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 33, 34 }, { 0x80000000 | 33, 35 },
        }}),
        // Slot 'layerItemChanged'
        QtMocHelpers::SlotData<void(QTreeWidgetItem *, int)>(36, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 33, 34 }, { QMetaType::Int, 37 },
        }}),
        // Slot 'refineVector'
        QtMocHelpers::SlotData<void()>(38, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'refineOverlapsLight'
        QtMocHelpers::SlotData<void()>(39, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'openAutoRoomsDialog'
        QtMocHelpers::SlotData<void()>(40, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<MainWindow, qt_meta_tag_ZN10MainWindowE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject MainWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10MainWindowE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10MainWindowE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN10MainWindowE_t>.metaTypes,
    nullptr
} };

void MainWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<MainWindow *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->applyCornerRadius(); break;
        case 1: _t->applyLineBend(); break;
        case 2: _t->promptForProjectUnits(); break;
        case 3: _t->setScaleInteractive(); break;
        case 4: _t->setDimPrecision((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 5: _t->changeFillPattern((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 6: _t->chooseColor(); break;
        case 7: _t->chooseFillColor(); break;
        case 8: _t->changeLineWidth((*reinterpret_cast< std::add_pointer_t<double>>(_a[1]))); break;
        case 9: _t->toggleGrid(); break;
        case 10: _t->newScene(); break;
        case 11: _t->openJson(); break;
        case 12: _t->saveJson(); break;
        case 13: _t->importSvg(); break;
        case 14: _t->exportSvg(); break;
        case 15: _t->runBluePrintAI(); break;
        case 16: _t->onVectoriseFinished(); break;
        case 17: _t->changeCornerRadius((*reinterpret_cast< std::add_pointer_t<double>>(_a[1]))); break;
        case 18: _t->zoomIn(); break;
        case 19: _t->zoomOut(); break;
        case 20: _t->zoomReset(); break;
        case 21: _t->zoomToFit(); break;
        case 22: _t->undo(); break;
        case 23: _t->redo(); break;
        case 24: _t->addLayer(); break;
        case 25: _t->removeSelectedLayer(); break;
        case 26: _t->setCurrentLayerFromTree((*reinterpret_cast< std::add_pointer_t<QTreeWidgetItem*>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QTreeWidgetItem*>>(_a[2]))); break;
        case 27: _t->layerItemChanged((*reinterpret_cast< std::add_pointer_t<QTreeWidgetItem*>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 28: _t->refineVector(); break;
        case 29: _t->refineOverlapsLight(); break;
        case 30: _t->openAutoRoomsDialog(); break;
        default: ;
        }
    }
}

const QMetaObject *MainWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MainWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10MainWindowE_t>.strings))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int MainWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 31)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 31;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 31)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 31;
    }
    return _id;
}
QT_WARNING_POP
