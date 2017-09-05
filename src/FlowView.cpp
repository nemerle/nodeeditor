#include "FlowView.hpp"

#include <QtWidgets/QGraphicsScene>

#include <QtGui/QPen>
#include <QtGui/QBrush>
#include <QtWidgets/QMenu>

#include <QtCore/QRectF>
#include <QtCore/QPointF>

#include <QtOpenGL>
#include <QtWidgets>

#include <QDebug>
#include <iostream>
#include <cmath>

#include "FlowScene.hpp"
#include "DataModelRegistry.hpp"
#include "Node.hpp"
#include "NodeGraphicsObject.hpp"
#include "ConnectionGraphicsObject.hpp"
#include "StyleCollection.hpp"

using QtNodes::FlowView;
using QtNodes::FlowScene;

FlowView::
FlowView(FlowScene *scene)
  : QGraphicsView(scene)
  , _scene(scene)
  , _clickPos(QPointF())
{
  setDragMode(QGraphicsView::ScrollHandDrag);
  setRenderHint(QPainter::Antialiasing);

  auto const &flowViewStyle = StyleCollection::flowViewStyle();

  setBackgroundBrush(flowViewStyle.BackgroundColor);

  //setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
  //setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  setTransformationAnchor(QGraphicsView::AnchorUnderMouse);

  setCacheMode(QGraphicsView::CacheBackground);

  //setViewport(new QGLWidget(QGLFormat(QGL::SampleBuffers)));

  // setup actions
  _clearSelectionAction = new QAction(QStringLiteral("Clear Selection"), this);
  _clearSelectionAction->setShortcut(Qt::Key_Escape);
  connect(_clearSelectionAction, &QAction::triggered, _scene, &QGraphicsScene::clearSelection);
  addAction(_clearSelectionAction);

  _deleteSelectionAction = new QAction(QStringLiteral("Delete Selection"), this);
  _deleteSelectionAction->setShortcut(Qt::Key_Delete);
  connect(_deleteSelectionAction, &QAction::triggered, this, &FlowView::deleteSelectedNodes);
  addAction(_deleteSelectionAction);
}


QAction*
FlowView::
clearSelectionAction() const
{
  return _clearSelectionAction;
}


QAction*
FlowView::
deleteSelectionAction() const
{
  return _deleteSelectionAction;
}


void
FlowView::
contextMenuEvent(QContextMenuEvent *event)
{
  QMenu modelMenu;

  auto skipText = QStringLiteral("skip me");

  //Add filterbox to the context menu
  auto *txtBox = new QLineEdit(&modelMenu);

  txtBox->setPlaceholderText(QStringLiteral("Filter"));
  txtBox->setClearButtonEnabled(true);

  auto *txtBoxAction = new QWidgetAction(&modelMenu);
  txtBoxAction->setDefaultWidget(txtBox);

  modelMenu.addAction(txtBoxAction);

  //Add result treeview to the context menu
  auto *treeView = new QTreeWidget(&modelMenu);
  treeView->header()->close();

  auto *treeViewAction = new QWidgetAction(&modelMenu);
  treeViewAction->setDefaultWidget(treeView);

  modelMenu.addAction(treeViewAction);

  std::unordered_map<QString, QTreeWidgetItem*> catergoryItems;
  for (auto const &modelName : _scene->model()->modelRegistry())
  {
    // get the catergory
    auto catergory = _scene->model()->nodeTypeCatergory(modelName);
    
    // see if it's already in the map
    auto iter = catergoryItems.find(catergory);
    
    // add it if it doesn't exist
    if (iter == catergoryItems.end()) {
        
        auto item = new QTreeWidgetItem(treeView);
        item->setText(0, catergory);
        item->setData(0, Qt::UserRole, skipText);
        
        iter = catergoryItems.emplace(catergory, item).first; 
    }
    
    // this is the catergory item
    auto parent = iter->second;
    
    // add the item
    

    auto item   = new QTreeWidgetItem(parent);
    item->setText(0, modelName);
    item->setData(0, Qt::UserRole, modelName);
  }

  treeView->expandAll();

  connect(treeView, &QTreeWidget::itemClicked, [&](QTreeWidgetItem *item, int column)
  {
    QString modelName = item->data(0, Qt::UserRole).toString();

    if (modelName == skipText)
    {
      return;
    }

    QPoint pos = event->pos();

    QPointF posView = this->mapToScene(pos);

    // try to create the node
    auto uuid = _scene->model()->addNode(modelName, posView);
    
    // if the node creation failed, then don't add it
    if (!uuid.isNull()) {
        // move it to the cursor location
        _scene->model()->moveNode(_scene->model()->nodeIndex(uuid), posView);
    }

    modelMenu.close();
  });

  //Setup filtering
  connect(txtBox, &QLineEdit::textChanged, [&](const QString &text)
  {
    for (auto& topLvlItem : catergoryItems)
    {
      for (int i = 0; i < topLvlItem.second->childCount(); ++i)
      {
        auto child = topLvlItem.second->child(i);
        auto modelName = child->data(0, Qt::UserRole).toString();
        if (modelName.contains(text, Qt::CaseInsensitive))
        {
          child->setHidden(false);
        }
        else
        {
          child->setHidden(true);
        }
      }
    }
  });

  // make sure the text box gets focus so the user doesn't have to click on it
  txtBox->setFocus();

  modelMenu.exec(event->globalPos());
}


void
FlowView::
wheelEvent(QWheelEvent *event)
{
  QPoint delta = event->angleDelta();

  if (delta.y() == 0)
  {
    event->ignore();
    return;
  }

  double const d = delta.y() / std::abs(delta.y());

  if (d > 0.0)
    scaleUp();
  else
    scaleDown();
}


void
FlowView::
scaleUp()
{
  double const step   = 1.2;
  double const factor = std::pow(step, 1.0);

  QTransform t = transform();

  if (t.m11() > 2.0)
    return;

  scale(factor, factor);
}


void
FlowView::
scaleDown()
{
  double const step   = 1.2;
  double const factor = std::pow(step, -1.0);

  scale(factor, factor);
}


void
FlowView::
deleteSelectedNodes()
{
  // delete the nodes, this will delete many of the connections
  for (QGraphicsItem * item : _scene->selectedItems())
  { 
    if (auto n = qgraphicsitem_cast<NodeGraphicsObject*>(item)) {
      auto index = n->index();
      
      flowScene().model()->removeNodeWithConnections(index);
    }
  }

  // now delete the selected connections
  for (QGraphicsItem * item : _scene->selectedItems())
  {
    if (auto c = qgraphicsitem_cast<ConnectionGraphicsObject*>(item)) {
      
      // does't matter if it works or doesn't, at least we tried
      flowScene().model()->removeConnection(c->node(PortType::Out), c->portIndex(PortType::Out), c->node(PortType::In), c->portIndex(PortType::In));
      
    }
  }
}


void
FlowView::
keyPressEvent(QKeyEvent *event)
{
  switch (event->key())
  {
    case Qt::Key_Shift:
      setDragMode(QGraphicsView::RubberBandDrag);
      break;

    default:
      break;
  }

  QGraphicsView::keyPressEvent(event);
}


void
FlowView::
keyReleaseEvent(QKeyEvent *event)
{
  switch (event->key())
  {
    case Qt::Key_Shift:
      setDragMode(QGraphicsView::ScrollHandDrag);
      break;

    default:
      break;
  }
  QGraphicsView::keyReleaseEvent(event);
}

void
FlowView::
mousePressEvent(QMouseEvent *event)
{
    QGraphicsView::mousePressEvent(event);
    if (event->button() == Qt::LeftButton) {
        _clickPos = mapToScene(event->pos());
    }
}

void
FlowView::
mouseMoveEvent(QMouseEvent *event)
{
    QGraphicsView::mouseMoveEvent(event);
    if (scene()->mouseGrabberItem() == nullptr && event->buttons() == Qt::LeftButton)
    {
        // Make sure shift is not being pressed
        if((event->modifiers() & Qt::ShiftModifier) == 0)
        {
            QPointF difference = _clickPos - mapToScene(event->pos());
            setSceneRect(sceneRect().translated(difference.x(), difference.y()));
        }
    }
}


void
FlowView::
drawBackground(QPainter* painter, const QRectF& r)
{
  QGraphicsView::drawBackground(painter, r);

  auto drawGrid =
    [&](double gridStep)
    {
      QRect   windowRect = rect();
      QPointF tl = mapToScene(windowRect.topLeft());
      QPointF br = mapToScene(windowRect.bottomRight());

      double left   = std::floor(tl.x() / gridStep - 0.5);
      double right  = std::floor(br.x() / gridStep + 1.0);
      double bottom = std::floor(tl.y() / gridStep - 0.5);
      double top    = std::floor (br.y() / gridStep + 1.0);

      // vertical lines
      for (int xi = int(left); xi <= int(right); ++xi)
      {
        QLineF line(xi * gridStep, bottom * gridStep,
                    xi * gridStep, top * gridStep );

        painter->drawLine(line);
      }

      // horizontal lines
      for (int yi = int(bottom); yi <= int(top); ++yi)
      {
        QLineF line(left * gridStep, yi * gridStep,
                    right * gridStep, yi * gridStep );
        painter->drawLine(line);
      }
    };

  auto const &flowViewStyle = StyleCollection::flowViewStyle();

  QBrush bBrush = backgroundBrush();

  QPen pfine(flowViewStyle.FineGridColor, 1.0);

  painter->setPen(pfine);
  drawGrid(15);

  QPen p(flowViewStyle.CoarseGridColor, 1.0);

  painter->setPen(p);
  drawGrid(150);
}


void
FlowView::
showEvent(QShowEvent *event)
{
  _scene->setSceneRect(this->rect());
  QGraphicsView::showEvent(event);
}
