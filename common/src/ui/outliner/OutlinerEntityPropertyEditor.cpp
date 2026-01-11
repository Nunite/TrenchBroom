#include "OutlinerEntityPropertyEditor.h"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "mdl/EntityDefinition.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/EntityProperties.h"
#include "mdl/Game.h"
#include "mdl/Map.h"
#include "mdl/Map_Entities.h"
#include "mdl/PropertyDefinition.h"
#include "ui/CollapsibleTitledPanel.h"
#include "ui/QtUtils.h"
#include "ui/SmartPropertyEditorManager.h"
#include "ui/SmartWadEditor.h"

#include <unordered_set>

namespace tb::ui
{
namespace
{
class OutlinerChoiceComboBox : public QComboBox
{
private:
    bool& m_popupVisible;
    OutlinerEntityPropertyEditor* m_editor;

public:
    OutlinerChoiceComboBox(bool& popupVisible, OutlinerEntityPropertyEditor* editor, QWidget* parent)
        : QComboBox{parent}
        , m_popupVisible{popupVisible}
        , m_editor{editor}
    {
    }

protected:
    void showPopup() override
    {
        m_popupVisible = true;
        QComboBox::showPopup();
    }

    void hidePopup() override
    {
        QComboBox::hidePopup();
        m_popupVisible = false;
        m_editor->onChoiceComboPopupHidden();
    }

    void wheelEvent(QWheelEvent* event) override
    {
        setFocus(Qt::MouseFocusReason);
        QComboBox::wheelEvent(event);
    }
};

struct ValueConsensus
{
    bool mixed = false;
    bool anyPresent = false;
    std::string value;
};

ValueConsensus consensusValue(
    const std::string& key, const std::vector<mdl::EntityNodeBase*>& nodes)
{
    auto result = ValueConsensus{};
    bool hasFirst = false;

    for (const auto* node : nodes)
    {
        const auto* v = node->entity().property(key);
        if (v != nullptr)
        {
            result.anyPresent = true;
        }

        if (!hasFirst)
        {
            if (v != nullptr)
            {
                result.value = *v;
            }
            hasFirst = true;
        }
        else
        {
            const auto current = v != nullptr ? *v : std::string{};
            if (current != result.value)
            {
                result.mixed = true;
            }
        }
    }

    if (!result.anyPresent)
    {
        result.mixed = false;
        result.value.clear();
    }

    return result;
}

std::vector<std::string> buildKeyOrder(const std::vector<mdl::EntityNodeBase*>& nodes)
{
    auto result = std::vector<std::string>{};
    auto seen = std::unordered_set<std::string>{};

    if (!nodes.empty())
    {
        for (const auto& prop : nodes.front()->entity().properties())
        {
            seen.insert(prop.key());
            result.push_back(prop.key());
        }
    }

    for (const auto* node : nodes)
    {
        for (const auto& prop : node->entity().properties())
        {
            if (seen.insert(prop.key()).second)
            {
                result.push_back(prop.key());
            }
        }
    }

    return result;
}

std::vector<std::string> buildKeyOrderWithInactiveDefinitions(
    const std::vector<mdl::EntityNodeBase*>& nodes,
    const mdl::EntityDefinition* definition)
{
    auto result = buildKeyOrder(nodes);

    if (!definition)
    {
        return result;
    }

    auto seen = std::unordered_set<std::string>{};
    for (const auto& key : result)
    {
        seen.insert(key);
    }

    for (const auto& propDef : definition->propertyDefinitions)
    {
        if (seen.insert(propDef.key).second)
        {
            result.push_back(propDef.key);
        }
    }

    return result;
}
} // namespace

OutlinerEntityPropertyEditor::OutlinerEntityPropertyEditor(mdl::Map& map, QWidget* parent)
    : QWidget{parent}
    , m_map{map}
{
    m_propertiesPanel = new CollapsibleTitledPanel{tr("Entity"), true, this};

    m_scrollArea = new QScrollArea{m_propertiesPanel->getPanel()};
    m_scrollArea->setObjectName("outlinerPropertyScrollArea");
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidgetResizable(true);

    m_scrollContents = new QWidget{m_scrollArea};
    m_scrollContents->setObjectName("outlinerPropertyCard");
    m_scrollContents->setAttribute(Qt::WA_StyledBackground, true);
    m_scrollLayout = new QVBoxLayout{m_scrollContents};
    m_scrollLayout->setContentsMargins(0, 0, 0, 0);
    m_scrollLayout->setSpacing(2);
    m_scrollLayout->addStretch(1);
    m_scrollArea->setWidget(m_scrollContents);

    auto* propertiesLayout = new QVBoxLayout{m_propertiesPanel->getPanel()};
    propertiesLayout->setContentsMargins(4, 4, 4, 4);
    propertiesLayout->setSpacing(0);
    propertiesLayout->addWidget(m_scrollArea, 1);

    m_smartEditorPanel = new CollapsibleTitledPanel{tr("Smart Editor"), true, this};
    m_smartEditorManager = new SmartPropertyEditorManager{m_map, m_smartEditorPanel->getPanel()};

    auto* smartLayout = new QVBoxLayout{m_smartEditorPanel->getPanel()};
    smartLayout->setContentsMargins(4, 4, 4, 4);
    smartLayout->setSpacing(0);
    smartLayout->addWidget(m_smartEditorManager, 1);

    auto* layout = new QVBoxLayout{this};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_propertiesPanel, 1);
    layout->addWidget(m_smartEditorPanel, 0);

    connectObservers();
    scheduleUpdate();
}

OutlinerEntityPropertyEditor::~OutlinerEntityPropertyEditor() = default;

void OutlinerEntityPropertyEditor::connectObservers()
{
    m_notifierConnection += m_map.selectionDidChangeNotifier.connect(
        [this](const mdl::SelectionChange&) { scheduleUpdate(); });
    m_notifierConnection += m_map.nodesDidChangeNotifier.connect(
        [this](const std::vector<mdl::Node*>&) { scheduleUpdate(); });
    m_notifierConnection +=
        m_map.entityDefinitionsDidChangeNotifier.connect([this]() { scheduleUpdate(); });
    m_notifierConnection += m_map.documentDidChangeNotifier.connect([this]() {
        if (m_embeddedWadEditor)
        {
            m_embeddedWadEditor->update(m_map.selection().allEntities());
        }
    });
}

void OutlinerEntityPropertyEditor::scheduleUpdate(const bool force)
{
    if (force)
    {
        m_forceUpdate = true;
    }

    if (m_updateQueued)
    {
        return;
    }

    m_updateQueued = true;
    QTimer::singleShot(0, this, [this]() {
        m_updateQueued = false;
        updateFromSelection();
    });
}

void OutlinerEntityPropertyEditor::onChoiceComboPopupHidden()
{
    if (!m_updateDeferred)
    {
        return;
    }

    const auto force = m_forceUpdate;
    m_updateDeferred = false;
    scheduleUpdate(force);
}

void OutlinerEntityPropertyEditor::updateFromSelection()
{
    if (m_comboPopupVisible)
    {
        m_updateDeferred = true;
        return;
    }

    if (!m_forceUpdate)
    {
        if (widgetOrChildHasFocus(this))
        {
            return;
        }
    }
    m_forceUpdate = false;
    m_updateDeferred = false;

    const auto entities = m_map.selection().allEntities();
    rebuildPropertyRows(entities);
    rebuildSmartEditor("");
}

void OutlinerEntityPropertyEditor::rebuildPropertyRows(
    const std::vector<mdl::EntityNodeBase*>& entityNodes)
{
    m_embeddedWadEditorContainer = nullptr;
    m_embeddedWadEditor = nullptr;

    const auto clearLayout = [&](auto&& self, QLayout* l) -> void {
        while (auto* item = l->takeAt(0))
        {
            if (auto* w = item->widget())
            {
                w->deleteLater();
            }
            if (auto* childLayout = item->layout())
            {
                self(self, childLayout);
                childLayout->deleteLater();
            }
            delete item;
        }
    };

    clearLayout(clearLayout, m_scrollLayout);

    auto* addRow = new QWidget{m_scrollContents};
    addRow->setObjectName("outlinerPropertyRow");
    addRow->setAttribute(Qt::WA_StyledBackground, true);
    auto* addLayout = new QHBoxLayout{addRow};
    addLayout->setContentsMargins(6, 4, 6, 4);
    addLayout->setSpacing(4);

    m_addKey = new QLineEdit{addRow};
    m_addKey->setObjectName("outlinerPropertyAddKey");
    m_addKey->setPlaceholderText(tr("Key"));
    m_addKey->setMinimumWidth(120);
    m_addKey->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    m_addValue = new QLineEdit{addRow};
    m_addValue->setObjectName("outlinerPropertyAddValue");
    m_addValue->setPlaceholderText(tr("Value"));
    m_addValue->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* addButton = createBitmapButton("Add.svg", tr("Add property"), addRow);
    addButton->setObjectName("toolButton_withBorder");
    addButton->setIconSize(QSize{16, 16});
    addButton->setFixedSize(QSize{24, 24});

    addLayout->addWidget(m_addKey);
    addLayout->addWidget(m_addValue, 1);
    addLayout->addWidget(addButton);

    m_scrollLayout->addWidget(addRow, 0);

    connect(addButton, &QAbstractButton::clicked, this, [this]() {
        const auto key = m_addKey->text().trimmed();
        if (key.isEmpty())
        {
            return;
        }

        mdl::setEntityProperty(m_map, key.toStdString(), m_addValue->text().toStdString(), false);
        m_addKey->clear();
        m_addValue->clear();
        scheduleUpdate(true);
    });

    if (entityNodes.empty())
    {
        auto* emptyLabel = new QLabel{tr("No entity selected"), m_scrollContents};
        emptyLabel->setObjectName("infoLabel");
        emptyLabel->setContentsMargins(4, 4, 4, 4);
        m_scrollLayout->addWidget(emptyLabel, 0);
        m_scrollLayout->addStretch(1);
        return;
    }

    const auto* entityDefinition = mdl::selectEntityDefinition(entityNodes);
    const auto keys = buildKeyOrderWithInactiveDefinitions(entityNodes, entityDefinition);
    const auto wadKey = m_map.game()->config().materialConfig.property;
    const auto canShowWadEditor = entityNodes.size() == 1
                                 && entityNodes.front()->entity().classname()
                                      == mdl::EntityPropertyValues::WorldspawnClassname;

    for (const auto& key : keys)
    {
        auto* row = new QWidget{m_scrollContents};
        row->setObjectName("outlinerPropertyRow");
        row->setAttribute(Qt::WA_StyledBackground, true);
        auto* rowLayout = new QHBoxLayout{row};
        rowLayout->setContentsMargins(6, 4, 6, 4);
        rowLayout->setSpacing(4);

        auto* keyLabel = new QLabel{QString::fromStdString(key), row};
        keyLabel->setObjectName("outlinerPropertyKey");
        keyLabel->setMinimumWidth(120);
        keyLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

        QLineEdit* valueEdit = nullptr;
        QComboBox* valueCombo = nullptr;

        const auto* propertyDef = mdl::selectPropertyDefinition(key, entityNodes);
        if (propertyDef)
        {
            if (
                const auto* choiceType =
                    std::get_if<mdl::PropertyValueTypes::Choice>(&propertyDef->valueType))
            {
                valueCombo = new OutlinerChoiceComboBox{
                    m_comboPopupVisible,
                    this,
                    row};
                valueCombo->setObjectName("outlinerPropertyValue");
                valueCombo->setEditable(true);
                valueCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                valueCombo->setProperty("propertyKey", QString::fromStdString(key));
                valueCombo->setProperty("suppressSmartEditor", true);

                for (const auto& option : choiceType->options)
                {
                    const auto value =
                        mapStringToUnicode(m_map.encoding(), option.value);
                    const auto label = mapStringToUnicode(
                        m_map.encoding(),
                        option.value + " : " + option.description);
                    valueCombo->addItem(label, value);
                }

                if (auto* comboLineEdit = valueCombo->lineEdit())
                {
                    comboLineEdit->setProperty("propertyKey", QString::fromStdString(key));
                    comboLineEdit->setProperty("suppressSmartEditor", true);
                }

                if (propertyDef->readOnly)
                {
                    valueCombo->setDisabled(true);
                }
            }
        }

        if (!valueCombo)
        {
            valueEdit = new QLineEdit{row};
            valueEdit->setObjectName("outlinerPropertyValue");
            valueEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            valueEdit->installEventFilter(this);
            valueEdit->setProperty("propertyKey", QString::fromStdString(key));

            if (propertyDef && propertyDef->readOnly)
            {
                valueEdit->setDisabled(true);
            }
        }

        const auto consensus = consensusValue(key, entityNodes);
        const auto inactive = propertyDef != nullptr && !consensus.anyPresent;
        if (inactive)
        {
            row->setProperty("inactive", true);
            keyLabel->setProperty("inactive", true);
            if (valueCombo)
            {
                valueCombo->setDisabled(true);
            }
            if (valueEdit)
            {
                valueEdit->setDisabled(true);
                valueEdit->setPlaceholderText(tr("<inactive>"));
            }
            if (valueCombo && valueCombo->lineEdit())
            {
                valueCombo->lineEdit()->setPlaceholderText(tr("<inactive>"));
            }
        }

        if (valueCombo)
        {
            if (auto* comboLineEdit = valueCombo->lineEdit())
            {
                if (consensus.mixed)
                {
                    const QSignalBlocker blocker{valueCombo};
                    valueCombo->setEditText(QString{});
                    comboLineEdit->setPlaceholderText(tr("<multiple>"));
                }
                else
                {
                    const QSignalBlocker blocker{valueCombo};
                    valueCombo->setEditText(
                        mapStringToUnicode(m_map.encoding(), consensus.value));
                    comboLineEdit->setPlaceholderText(QString{});
                }
            }
        }
        else if (valueEdit)
        {
            if (!inactive && consensus.mixed)
            {
                valueEdit->clear();
                valueEdit->setPlaceholderText(tr("<multiple>"));
            }
            else if (!inactive)
            {
                valueEdit->setText(QString::fromStdString(consensus.value));
                valueEdit->setPlaceholderText(QString{});
            }
        }

        auto* activateButton = createBitmapButton("Add.svg", tr("Activate property"), row);
        activateButton->setObjectName("toolButton_withBorder");
        activateButton->setIconSize(QSize{16, 16});
        activateButton->setFixedSize(QSize{24, 24});
        activateButton->setProperty("propertyKey", QString::fromStdString(key));
        activateButton->setHidden(!inactive);

        auto* removeButton = createBitmapButton("Remove.svg", tr("Remove property"), row);
        removeButton->setObjectName("toolButton_withBorder");
        removeButton->setIconSize(QSize{16, 16});
        removeButton->setFixedSize(QSize{24, 24});
        removeButton->setProperty("propertyKey", QString::fromStdString(key));
        removeButton->setHidden(inactive);
        if (propertyDef && propertyDef->readOnly)
        {
            removeButton->setDisabled(true);
        }

        QToolButton* wadToggleButton = nullptr;
        if (canShowWadEditor && wadKey && key == *wadKey)
        {
            wadToggleButton = new QToolButton{row};
            wadToggleButton->setObjectName("toolButton_withBorder");
            wadToggleButton->setCheckable(true);
            wadToggleButton->setFixedSize(QSize{24, 24});
            wadToggleButton->setToolTip(tr("Show wad file editor"));

            const QSignalBlocker blocker{wadToggleButton};
            wadToggleButton->setChecked(m_wadEditorExpanded);
            wadToggleButton->setArrowType(m_wadEditorExpanded ? Qt::DownArrow : Qt::RightArrow);
        }

        rowLayout->addWidget(keyLabel);
        if (valueCombo)
        {
            rowLayout->addWidget(valueCombo, 1);
        }
        else
        {
            rowLayout->addWidget(valueEdit, 1);
        }
        if (wadToggleButton)
        {
            rowLayout->addWidget(wadToggleButton);
        }
        rowLayout->addWidget(activateButton);
        rowLayout->addWidget(removeButton);

        m_scrollLayout->addWidget(row, 0);

        if (wadToggleButton)
        {
            auto* container = new QWidget{m_scrollContents};
            container->setObjectName("outlinerEmbeddedWadEditor");
            auto* containerLayout = new QVBoxLayout{container};
            containerLayout->setContentsMargins(6, 0, 6, 4);
            containerLayout->setSpacing(0);

            m_embeddedWadEditorContainer = container;
            m_embeddedWadEditor = new SmartWadEditor{m_map, container, false};
            m_embeddedWadEditor->activate(*wadKey);
            m_embeddedWadEditor->update(entityNodes);
            containerLayout->addWidget(m_embeddedWadEditor, 1);

            container->setVisible(m_wadEditorExpanded);
            m_scrollLayout->addWidget(container, 0);

            connect(wadToggleButton, &QToolButton::toggled, this, [this, container, wadToggleButton](const bool checked) {
                m_wadEditorExpanded = checked;
                wadToggleButton->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
                container->setVisible(checked);
                rebuildSmartEditor("");
            });
        }

        connect(activateButton, &QAbstractButton::clicked, this, [this, activateButton, propertyDef]() {
            const auto keyVariant = activateButton->property("propertyKey");
            if (!keyVariant.isValid())
            {
                return;
            }

            auto value = std::string{};
            if (propertyDef)
            {
                value = mdl::PropertyDefinition::defaultValue(*propertyDef).value_or("");
            }

            mdl::setEntityProperty(m_map, keyVariant.toString().toStdString(), value, false);
            scheduleUpdate(true);
        });

        if (valueCombo)
        {
            connect(
                valueCombo,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this,
                [this, valueCombo](const int index) {
                    const auto keyVariant = valueCombo->property("propertyKey");
                    if (!keyVariant.isValid())
                    {
                        return;
                    }

                    if (index < 0)
                    {
                        return;
                    }

                    const auto valueVariant = valueCombo->itemData(index);
                    if (!valueVariant.isValid())
                    {
                        return;
                    }

                    valueCombo->setFocus(Qt::MouseFocusReason);

                    const auto value = valueVariant.toString();
                    {
                        const QSignalBlocker blocker{valueCombo};
                        valueCombo->setEditText(value);
                    }

                    mdl::setEntityProperty(
                        m_map,
                        keyVariant.toString().toStdString(),
                        mapStringFromUnicode(m_map.encoding(), value),
                        false);
                });

            if (auto* comboLineEdit = valueCombo->lineEdit())
            {
                connect(comboLineEdit, &QLineEdit::editingFinished, this, [this, valueCombo]() {
                    const auto keyVariant = valueCombo->property("propertyKey");
                    if (!keyVariant.isValid())
                    {
                        return;
                    }

                    mdl::setEntityProperty(
                        m_map,
                        keyVariant.toString().toStdString(),
                        mapStringFromUnicode(m_map.encoding(), valueCombo->currentText()),
                        false);
                    scheduleUpdate(true);
                });
            }
        }
        else
        {
            connect(valueEdit, &QLineEdit::editingFinished, this, [this, valueEdit]() {
                const auto keyVariant = valueEdit->property("propertyKey");
                if (!keyVariant.isValid())
                {
                    return;
                }
                const auto key = keyVariant.toString().toStdString();
                mdl::setEntityProperty(m_map, key, valueEdit->text().toStdString(), false);
                scheduleUpdate(true);
            });
        }

        connect(removeButton, &QAbstractButton::clicked, this, [this, removeButton]() {
            const auto keyVariant = removeButton->property("propertyKey");
            if (!keyVariant.isValid())
            {
                return;
            }
            const auto key = keyVariant.toString().toStdString();
            mdl::removeEntityProperty(m_map, key);
            scheduleUpdate(true);
        });
    }

    m_scrollLayout->addStretch(1);
}

void OutlinerEntityPropertyEditor::rebuildSmartEditor(const std::string& propertyKey)
{
    if (propertyKey.empty())
    {
        m_smartEditorManager->switchEditor("", m_map.selection().allEntities());
    }
    else
    {
        m_smartEditorManager->switchEditor(propertyKey, m_map.selection().allEntities());
    }

    m_smartEditorPanel->setHidden(m_smartEditorManager->isDefaultEditorActive());
}

bool OutlinerEntityPropertyEditor::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FocusIn)
    {
        const auto suppress = watched->property("suppressSmartEditor");
        if (suppress.isValid() && suppress.toBool())
        {
            return QWidget::eventFilter(watched, event);
        }

        const auto keyVariant = watched->property("propertyKey");
        if (keyVariant.isValid())
        {
            const auto key = keyVariant.toString().toStdString();
            const auto wadKey = m_map.game()->config().materialConfig.property;
            if (wadKey && key == *wadKey)
            {
                rebuildSmartEditor("");
            }
            else
            {
                rebuildSmartEditor(key);
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

} // namespace tb::ui
