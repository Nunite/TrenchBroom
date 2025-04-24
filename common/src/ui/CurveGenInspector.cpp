//Added by Lws

#include "CurveGenInspector.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QLabel>
#include <QProcess>
#include <QMessageBox>
#include <QTextEdit>
#include <QFont>
#include <QTextCursor>
#include <QApplication>
#include <exception>  // Standard exception library
#include <QTimer>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QComboBox>
#include <QListWidget>
#include <QTabWidget>
#include <QFrame>
#include <QCheckBox>
#include <QGroupBox>
#include <QScrollArea>
#include <QSpacerItem>

#include "ui/MapDocument.h"
#include "ui/QtUtils.h"
#include "ui/Splitter.h"
#include "ui/Selection.h" // Ensure this is included
#include "ui/CompilationVariables.h" // Include CompilationVariables

namespace tb::ui
{

// Define the supported global commands and their explanations
const QList<QPair<QString, QString>> GLOBAL_COMMANDS = {
    {"source", QObject::tr("Custom input map file path. Example: C:\\MyMaps\\MySourceMap.map")},
    {"target", QObject::tr("Custom output map file path. Example: C:\\MyMaps\\MyTargetMap.map")},
    {"append", QObject::tr("Whether to append to target (0: overwrite, 1: append)")},
    {"obj", QObject::tr("Whether to export OBJ file (0: no, 1: yes)")}
};

// Define the supported curve parameters and their explanations
const QList<QPair<QString, QString>> CURVE_COMMANDS = {
    // 基本曲线参数
    {"rad", QObject::tr("Curve radius. 0=original radius, >0=custom units")},
    {"splinefile", QObject::tr("Custom spline file path")},
    {"nulltex", QObject::tr("Default texture name for triangulation")},
    {"append", QObject::tr("Whether to append to target (0: overwrite, 1: append)")},
    {"offset", QObject::tr("Radius offset, can be positive or negative")},
    {"height", QObject::tr("Height increase per segment, effective when >0")},
    {"p_expand", QObject::tr("Expand path object")},
    {"range_start", QObject::tr("Export start percentage (0-100)")},
    {"range_end", QObject::tr("Export end percentage (0-100)")},
    {"spike_height", QObject::tr("Triangulation spike height, default 4")},
    {"hshiftoffset", QObject::tr("Horizontal shift offset")},
    {"scale", QObject::tr("Scaling for final generated curve object")},
    {"scale_src", QObject::tr("Scaling for source map")},
    {"d_pos", QObject::tr("Detail position along curve section (0-1)")},
    {"res", QObject::tr("Number of segments (4-384), 0=inherit from previous")},
    {"obj", QObject::tr("Whether to export OBJ file (0: no, 1: yes)")},
    {"map", QObject::tr("Whether to generate map output (0: no, 1: yes)")},
    {"rmf", QObject::tr("Whether to generate RMF output (0: no, 1: yes)")},
    {"type", QObject::tr("Construction type (0: Pi circle, 1: Grid circle, 2: Path)")},
    {"shift", QObject::tr("Texture horizontal mode (0: none, 1: per segment, 2: per brush, 3: per brush texture, 4: left align, 5: per group texture)")},
    {"tri", QObject::tr("Triangulation (0: no, 1: yes)")},
    {"round", QObject::tr("Round coordinates (0: no, 1: yes)")},
    {"ramp", QObject::tr("Ramp mode (0: none, 1: linear, 2: smooth)")},
    {"p_cornerfix", QObject::tr("Fix overlapping corners in path extrusion (0: no, 1: yes)")},
    {"p_reverse", QObject::tr("Reverse direction of path (0: no, 1: yes)")},
    {"p_split", QObject::tr("Split path objects on export (0: no, 1: yes)")},
    {"p_evenout", QObject::tr("Even out path points (0: no, 1: yes)")},
    {"bounds", QObject::tr("Generate bounding box (0: no, 1: yes)")},
    {"transit_tri", QObject::tr("Triangulation at start/end (0: no, 1: yes)")},
    {"transit_round", QObject::tr("Round coordinates at start/end (0: no, 1: yes)")},
    {"skipnull", QObject::tr("Skip all NULL brushes (0: no, 1: yes)")},
    // 详细对象选项
    {"c_enable", QObject::tr("Enable curve generation (0: no, 1: yes)")},
    {"d_enable", QObject::tr("Enable detail objects (0: no, 1: yes)")},
    {"d_autoyaw", QObject::tr("Auto-adjust yaw for details (0: no, 1: yes)")},
    {"d_autopitch", QObject::tr("Auto-adjust pitch for details (0: no, 1: yes)")},
    {"d_separate", QObject::tr("Generate details as separate objects (0: no, 1: yes)")},
    {"d_autoname", QObject::tr("Auto-name detail objects (0: no, 1: yes)")},
    {"d_draw", QObject::tr("Draw every n-th detail element")},
    {"d_draw_rand", QObject::tr("Randomly draw detail elements (0: no, 1: yes)")},
    {"d_skip", QObject::tr("Skip every n-th detail element")},
    {"d_carve", QObject::tr("Carve detail elements into curve (0: no, 1: yes)")},
    {"d_autoassign", QObject::tr("Auto-assign detail properties (0: no, 1: yes)")},
    {"d_circlemode", QObject::tr("Circle mode for detail placement (0: linear, 1: circular)")},
    // 高级纹理和形状选项
    {"heightmode", QObject::tr("Height adjustment mode")},
    {"texmode", QObject::tr("Texture application mode")},
    {"flatcircle", QObject::tr("Generate flat circle instead of 3D curve (0: no, 1: yes)")},
    {"hstretch", QObject::tr("Horizontal stretch mode (0: no, 1: yes)")},
    {"hstretchamt", QObject::tr("Horizontal stretch amount")},
    {"hshiftsrc", QObject::tr("Horizontal shift source")},
    // 变换参数
    {"rot", QObject::tr("Rotation for final curve object (XYZ order)")},
    {"rot_src", QObject::tr("Rotation for source map (XYZ order)")},
    {"move", QObject::tr("Translation for final curve object")},
    {"d_pos_rand", QObject::tr("Random position variation for details")},
    {"d_rotz_rand", QObject::tr("Random rotation Z variation for details")},
    {"d_movey_rand", QObject::tr("Random Y movement for details")},
    {"d_scale_rand", QObject::tr("Random scale variation for details")},
    {"p_scale", QObject::tr("Path scale")},
    {"gridsize", QObject::tr("Grid size for curve generation")}
};

// Define enumerations for dropdown menus
const QMap<QString, QList<QPair<QString, QString>>> ENUM_OPTIONS = {
    {"shift", {
        {"0", QObject::tr("None")},
        {"1", QObject::tr("Per segment")},
        {"2", QObject::tr("Per brush")},
        {"3", QObject::tr("Per brush texture")},
        {"4", QObject::tr("Left align")},
        {"5", QObject::tr("Per group texture")}
    }},
    {"type", {
        {"0", QObject::tr("Pi circle")},
        {"1", QObject::tr("Grid circle")},
        {"2", QObject::tr("Path")}
    }},
    {"ramp", {
        {"0", QObject::tr("None")},
        {"1", QObject::tr("Linear")},
        {"2", QObject::tr("Smooth")}
    }},
    {"append", {
        {"0", QObject::tr("Overwrite")},
        {"1", QObject::tr("Append")}
    }},
    {"obj", {
        {"0", QObject::tr("No")},
        {"1", QObject::tr("Yes")}
    }},
    {"ramptex", {
        {"0", QObject::tr("Diagonal")},
        {"1", QObject::tr("Square")}
    }},
    {"round", {
        {"0", QObject::tr("No")},
        {"1", QObject::tr("Yes")}
    }},
    {"tri", {
        {"0", QObject::tr("No")},
        {"1", QObject::tr("Yes")}
    }},
    {"bounds", {
        {"0", QObject::tr("No")},
        {"1", QObject::tr("Yes")}
    }},
    {"skipnull", {
        {"0", QObject::tr("No")},
        {"1", QObject::tr("Yes")}
    }},
    {"gaps", {
        {"0", QObject::tr("No")},
        {"1", QObject::tr("Yes")}
    }},
    {"transit_tri", {
        {"0", QObject::tr("No")},
        {"1", QObject::tr("Yes")}
    }},
    {"transit_round", {
        {"0", QObject::tr("No")},
        {"1", QObject::tr("Yes")}
    }},
    {"face_basis", {
        {"0", QObject::tr("Automatic")},
        {"1", QObject::tr("Manual")},
        {"2", QObject::tr("Mapped")}
    }},
    {"mirror", {
        {"0", QObject::tr("None")},
        {"1", QObject::tr("X-axis")},
        {"2", QObject::tr("Y-axis")},
        {"3", QObject::tr("Z-axis")}
    }},
    {"cap", {
        {"0", QObject::tr("None")},
        {"1", QObject::tr("Start")},
        {"2", QObject::tr("End")},
        {"3", QObject::tr("Both")}
    }},
    {"notex", {
        {"0", QObject::tr("Apply textures")},
        {"1", QObject::tr("Don't apply")}
    }},
    {"extrude", {
        {"0", QObject::tr("No")},
        {"1", QObject::tr("Yes")}
    }},
    {"tilttex", {
        {"0", QObject::tr("No")},
        {"1", QObject::tr("Yes")}
    }},
    {"stretch_axis", {
        {"0", QObject::tr("X-axis")},
        {"1", QObject::tr("Y-axis")},
        {"2", QObject::tr("Z-axis")}
    }},
    {"spiral_axis", {
        {"0", QObject::tr("X-axis")},
        {"1", QObject::tr("Y-axis")},
        {"2", QObject::tr("Z-axis")}
    }},
    {"face_align", {
        {"0", QObject::tr("Tangent")},
        {"1", QObject::tr("Normal")},
        {"2", QObject::tr("Binormal")}
    }},
    {"plane_center", {
        {"0", QObject::tr("First point")},
        {"1", QObject::tr("Center of segment")}
    }},
    {"track_pivot", {
        {"0", QObject::tr("No")},
        {"1", QObject::tr("Yes")}
    }},
    {"p_reverse", {
        {"0", QObject::tr("No")},
        {"1", QObject::tr("Yes")}
    }},
    {"p_cornerfix", {
        {"0", QObject::tr("No")},
        {"1", QObject::tr("Yes")}
    }},
    {"p_split", {
        {"0", QObject::tr("No")},
        {"1", QObject::tr("Yes")}
    }}
};

CurveGenInspector::CurveGenInspector(
  std::weak_ptr<MapDocument> document, GLContextManager& contextManager, QWidget* parent)
  : TabBookPage{parent}
  , m_document{document} // Added
  , m_process{nullptr}
{
  createGui(document, contextManager);
}

void CurveGenInspector::createGui(
  std::weak_ptr<MapDocument> document, GLContextManager& contextManager)
{
  m_splitter = new Splitter{Qt::Vertical};
  m_splitter->setObjectName("CurveGenInspector");

  // Original display area - for showing selected content
  m_selectionView = new QTextEdit{};
  m_selectionView->setReadOnly(true);
  m_splitter->addWidget(m_selectionView);

  // Create a tab widget to switch between tool execution and preset editor
  m_tabWidget = new QTabWidget{};
  
  // Create tool execution panel
  m_toolPanel = new QWidget{};
  createToolExecutionPanel();
  
  // Create preset editor panel
  m_presetPanel = new QWidget{};
  createPresetEditorPanel();
  
  // Add tabs
  m_tabWidget->addTab(m_toolPanel, tr("Tool Execution"));
  m_tabWidget->addTab(m_presetPanel, tr("Preset Editor"));
  
  m_splitter->addWidget(m_tabWidget);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(m_splitter, 1);
  setLayout(layout);
  
  // Set reasonable initial size proportions for the splitter
  m_splitter->setStretchFactor(0, 1); // Selection content area takes less space
  m_splitter->setStretchFactor(1, 3); // Tool panel takes more space

  restoreWindowState(m_splitter);
  
  // Initialize curve list with an empty curve
  m_curvesList.append(QMap<QString, QString>());
  updateCurveEditor();
}

void CurveGenInspector::createPresetEditorPanel()
{
  // Main layout for the preset editor panel
  QVBoxLayout* mainLayout = new QVBoxLayout(m_presetPanel);
  mainLayout->setContentsMargins(10, 10, 10, 10);
  mainLayout->setSpacing(10);
  
  // 添加曲线选择下拉框
  QHBoxLayout* curveSelectLayout = new QHBoxLayout();
  QLabel* curveLabel = new QLabel(tr("Current Curve:"));
  m_curveComboBox = new QComboBox();
  m_curveComboBox->addItem(tr("Curve #1"));
  curveSelectLayout->addWidget(curveLabel);
  curveSelectLayout->addWidget(m_curveComboBox, 1);
  
  // 添加、删除和加载预设的按钮
  QPushButton* addButton = new QPushButton(tr("Add"));
  QPushButton* deleteButton = new QPushButton(tr("Delete"));
  QPushButton* loadPresetButton = new QPushButton(tr("Load Preset"));
  curveSelectLayout->addWidget(addButton);
  curveSelectLayout->addWidget(deleteButton);
  curveSelectLayout->addWidget(loadPresetButton);
  
  // 连接信号槽
  connect(addButton, &QPushButton::clicked, this, &CurveGenInspector::addCurve);
  connect(deleteButton, &QPushButton::clicked, this, &CurveGenInspector::deleteCurve);
  connect(loadPresetButton, &QPushButton::clicked, this, &CurveGenInspector::loadPreset);
  connect(m_curveComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), 
          this, &CurveGenInspector::curveSelectionChanged);
  
  mainLayout->addLayout(curveSelectLayout);
  
  // Global commands section
  QGroupBox* globalGroup = new QGroupBox(tr("Global Settings"));
  setupGlobalCommandsUI(globalGroup);
  
  // Curve parameters section
  QGroupBox* curveGroup = new QGroupBox(tr("Curve Parameters"));
  setupCurveParametersUI(curveGroup);
  
  // Add widgets to layout
  mainLayout->addWidget(globalGroup);
  mainLayout->addWidget(curveGroup, 1);
  
  // Initialize the UI with default values
  m_currentCurveIndex = 0;
}

void CurveGenInspector::setupGlobalCommandsUI(QWidget* parent)
{
  // Create a grid layout for global commands
  QGridLayout* layout = new QGridLayout(parent);
  layout->setContentsMargins(10, 15, 10, 10);
  
  // Add global command fields
  int row = 0;
  for (const auto& cmd : GLOBAL_COMMANDS) {
    QString name = cmd.first;
    QString description = cmd.second;
    
    QLabel* label = new QLabel(name);
    label->setMinimumWidth(80);
    layout->addWidget(label, row, 0);
    
    // Check if this command should be a combo box
    if (ENUM_OPTIONS.contains(name)) {
      QComboBox* combo = new QComboBox();
      combo->setMinimumWidth(120);
      
      // Add options to the combo box
      for (const auto& option : ENUM_OPTIONS[name]) {
        combo->addItem(option.first + ": " + option.second, option.first);
      }
      
      layout->addWidget(combo, row, 1);
      m_globalCommandCombos[name] = combo;
    } else {
      QLineEdit* edit = new QLineEdit();
      edit->setMinimumWidth(120);
      layout->addWidget(edit, row, 1);
      m_globalCommandEdits[name] = edit;
    }
    
    // 创建问号按钮，替代原来的描述标签
    QPushButton* helpBtn = new QPushButton("?");
    helpBtn->setMaximumWidth(20);
    helpBtn->setToolTip(description);
    helpBtn->setStyleSheet("QPushButton { font-weight: bold; border-radius: 10px; }");
    helpBtn->setCursor(Qt::WhatsThisCursor);
    layout->addWidget(helpBtn, row, 2);
    
    row++;
  }
}

void CurveGenInspector::setupCurveParametersUI(QWidget* parent)
{
  // 创建主垂直布局
  QVBoxLayout* mainLayout = new QVBoxLayout(parent);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  
  // 创建滚动区域
  QScrollArea* scrollArea = new QScrollArea();
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  
  // 创建容器widget，用于放置所有参数
  QWidget* containerWidget = new QWidget();
  QGridLayout* layout = new QGridLayout(containerWidget);
  layout->setContentsMargins(10, 15, 10, 10);
  
  // 将容器添加到滚动区域
  scrollArea->setWidget(containerWidget);
  
  // 将参数分组
  QStringList basicParams = {"rad", "type", "offset", "res", "shift", "height", "ramp", "round", "tri"};
  QStringList rangeParams = {"range_start", "range_end", "bounds", "spike_height", "nulltex", "skipnull", "transit_tri", "transit_round"};
  QStringList textureParams = {"hshiftoffset", "texmode", "hstretch", "hstretchamt", "hshiftsrc"};
  QStringList pathParams = {"p_expand", "p_reverse", "p_cornerfix", "p_split", "p_evenout", "p_scale", "splinefile"};
  QStringList detailParams = {"c_enable", "d_enable", "d_pos", "d_autoyaw", "d_autopitch", "d_separate", "d_autoname", "d_draw", "d_draw_rand", "d_skip", "d_carve", "d_autoassign", "d_circlemode", "d_pos_rand", "d_rotz_rand", "d_movey_rand", "d_scale_rand"};
  QStringList shapeParams = {"heightmode", "flatcircle"};
  QStringList exportParams = {"append", "obj", "map", "rmf"};
  QStringList transformParams = {"scale", "scale_src", "rot", "rot_src", "move", "gridsize"};
  
  // 创建分组区域
  QMap<QString, QList<QPair<QString, QString>>> paramGroups = {
    {tr("Basic Parameters"), {}},
    {tr("Range and Boundaries"), {}},
    {tr("Texture Controls"), {}},
    {tr("Path Controls"), {}},
    {tr("Detail Objects"), {}},
    {tr("Shape Options"), {}},
    {tr("Export Options"), {}},
    {tr("Transformations"), {}}
  };
  
  // 将参数分配到相应的组
  for (const auto& cmd : CURVE_COMMANDS) {
    QString name = cmd.first;
    QString description = cmd.second;
    
    if (basicParams.contains(name)) {
      paramGroups[tr("Basic Parameters")].append(cmd);
    } else if (rangeParams.contains(name)) {
      paramGroups[tr("Range and Boundaries")].append(cmd);
    } else if (textureParams.contains(name)) {
      paramGroups[tr("Texture Controls")].append(cmd);
    } else if (transformParams.contains(name)) {
      paramGroups[tr("Transformations")].append(cmd);
    } else if (detailParams.contains(name)) {
      paramGroups[tr("Detail Objects")].append(cmd);
    } else if (shapeParams.contains(name)) {
      paramGroups[tr("Shape Options")].append(cmd);
    } else if (exportParams.contains(name)) {
      paramGroups[tr("Export Options")].append(cmd);
    } else if (pathParams.contains(name)) {
      paramGroups[tr("Path Controls")].append(cmd);
    } else {
      // 如果没有匹配的组，放入基本参数组
      paramGroups[tr("Basic Parameters")].append(cmd);
    }
  }
  
  // 创建手风琴式折叠面板
  int row = 0;
  for (auto groupIt = paramGroups.begin(); groupIt != paramGroups.end(); ++groupIt) {
    QString groupName = groupIt.key();
    const QList<QPair<QString, QString>>& params = groupIt.value();
    
    if (params.isEmpty()) continue; // 跳过空组
    
    // 创建组框架
    QGroupBox* groupBox = new QGroupBox(groupName);
    groupBox->setStyleSheet("QGroupBox { font-weight: bold; }");
    groupBox->setCheckable(true);
    groupBox->setChecked(groupName == tr("Basic Parameters")); // 默认只展开基本参数
    
    // 创建组内布局
    QGridLayout* groupLayout = new QGridLayout(groupBox);
    groupLayout->setContentsMargins(10, 20, 10, 10);
    
    // 添加组内参数
    int groupRow = 0;
    for (const auto& param : params) {
      QString name = param.first;
      QString description = param.second;
      
      QLabel* label = new QLabel(name);
      label->setMinimumWidth(80);
      groupLayout->addWidget(label, groupRow, 0);
      
      // 检查是否应该使用下拉框
      if (ENUM_OPTIONS.contains(name)) {
        QComboBox* combo = new QComboBox();
        combo->setMinimumWidth(120);
        
        // 添加下拉选项
        for (const auto& option : ENUM_OPTIONS[name]) {
          combo->addItem(option.first + ": " + option.second, option.first);
        }
        
        groupLayout->addWidget(combo, groupRow, 1);
        m_curveParamCombos[name] = combo;
      } else {
        QLineEdit* edit = new QLineEdit();
        edit->setMinimumWidth(120);
        groupLayout->addWidget(edit, groupRow, 1);
        m_curveParamEdits[name] = edit;
      }
      
      // 创建问号按钮，显示参数描述
      QPushButton* helpBtn = new QPushButton("?");
      helpBtn->setMaximumWidth(20);
      helpBtn->setToolTip(description);
      helpBtn->setStyleSheet("QPushButton { font-weight: bold; border-radius: 10px; }");
      helpBtn->setCursor(Qt::WhatsThisCursor);
      groupLayout->addWidget(helpBtn, groupRow, 2);
      
      groupRow++;
    }
    
    // 连接折叠信号
    connect(groupBox, &QGroupBox::toggled, [groupBox](bool checked) {
      for (int i = 0; i < groupBox->layout()->count(); ++i) {
        QWidget* widget = groupBox->layout()->itemAt(i)->widget();
        if (widget) {
          widget->setVisible(checked);
        }
      }
    });
    
    // 初始状态，隐藏非基本参数组中的控件
    if (groupName != tr("Basic Parameters")) {
      for (int i = 0; i < groupBox->layout()->count(); ++i) {
        QWidget* widget = groupBox->layout()->itemAt(i)->widget();
        if (widget) {
          widget->setVisible(false);
        }
      }
    }
    
    // 添加组到主布局
    layout->addWidget(groupBox, row++, 0, 1, 3);
  }
  
  // 在底部添加一个弹性空间，确保内容可以向上推
  layout->addItem(new QSpacerItem(0, 20, QSizePolicy::Minimum, QSizePolicy::Expanding), row, 0);
  
  // 将滚动区域添加到主布局
  mainLayout->addWidget(scrollArea);
}

void CurveGenInspector::addCurve()
{
  // Add a new empty curve to the list
  int newIndex = m_curvesList.size();
  m_curvesList.append(QMap<QString, QString>());
  
  // 添加新项到下拉框
  m_curveComboBox->addItem(tr("Curve #%1").arg(newIndex + 1));
  
  // 选择新添加的曲线
  m_curveComboBox->setCurrentIndex(newIndex);
}

void CurveGenInspector::deleteCurve()
{
  int currentIndex = m_curveComboBox->currentIndex();
  
  // 防止删除最后一条曲线
  if (m_curvesList.size() > 1) {
    // 从数据列表中移除
    m_curvesList.removeAt(currentIndex);
    
    // 从UI下拉框中移除
    m_curveComboBox->removeItem(currentIndex);
    
    // 重命名剩余曲线
    for (int i = 0; i < m_curveComboBox->count(); i++) {
      m_curveComboBox->setItemText(i, tr("Curve #%1").arg(i + 1));
    }
    
    // 如果删除的是最后一项，选择新的最后一项
    if (currentIndex >= m_curveComboBox->count()) {
      m_curveComboBox->setCurrentIndex(m_curveComboBox->count() - 1);
    } else {
      m_curveComboBox->setCurrentIndex(currentIndex);
    }
  }
}

void CurveGenInspector::loadPreset()
{
  // 打开文件选择对话框
  QString presetFilePath = QFileDialog::getOpenFileName(
    this,
    tr("Load Map2Curve Preset"),
    QString{},
    tr("Preset Files (*.txt);;All Files (*.*)"));
    
  if (presetFilePath.isEmpty()) {
    return; // 用户取消了选择
  }
  
  // 打开文件
  QFile presetFile(presetFilePath);
  if (!presetFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QMessageBox::warning(this, tr("Error"), tr("Could not open preset file: %1").arg(presetFilePath));
    return;
  }
  
  // 读取文件内容
  QTextStream in(&presetFile);
  QString fileContent = in.readAll();
  presetFile.close();
  
  // 验证文件格式
  if (!fileContent.contains("This Preset File was generated with Map2Curve GUI")) {
    QMessageBox::warning(this, tr("Invalid Preset File"), 
                         tr("The selected file does not appear to be a valid Map2Curve preset file."));
    return;
  }
  
  // 解析文件内容
  QStringList lines = fileContent.split('\n');
  
  // 初始化曲线列表
  m_curvesList.clear();
  m_curveComboBox->clear();
  
  // 全局设置
  QMap<QString, QString> globalSettings;
  
  // 当前在处理的曲线
  int currentCurveIndex = -1;
  QMap<QString, QString> currentCurve;
  QString curveName;
  
  // 处理每一行
  for (const QString& line : lines) {
    QString trimmedLine = line.trimmed();
    
    // 跳过空行和注释行
    if (trimmedLine.isEmpty() || trimmedLine.startsWith("//")) {
      // 检查曲线对象标记
      if (trimmedLine.contains("++++++++++")) {
        // 如果有正在处理的曲线，保存它
        if (currentCurveIndex >= 0 && !currentCurve.isEmpty()) {
          m_curvesList.append(currentCurve);
          m_curveComboBox->addItem(curveName.isEmpty() ? tr("Curve #%1").arg(currentCurveIndex + 1) : curveName);
        }
        
        // 准备新的曲线
        currentCurveIndex++;
        currentCurve.clear();
        curveName = "";
      }
      continue;
    }
    
    // 分割键值对
    int tabPos = trimmedLine.indexOf('\t');
    if (tabPos == -1) continue;
    
    QString key = trimmedLine.left(tabPos).trimmed();
    QString value = trimmedLine.mid(tabPos).trimmed();
    
    // 如果值以多个制表符开头，删除它们
    while (value.startsWith('\t')) {
      value = value.mid(1);
    }
    
    // 去除值两边的引号
    if (value.startsWith('"') && value.endsWith('"')) {
      value = value.mid(1, value.length() - 2);
    }
    
    // 如果是曲线名称
    if (key == "name") {
      curveName = value;
      continue;
    }
    
    // 根据当前输入位置存储参数
    if (currentCurveIndex >= 0) {
      // 当前正在处理曲线参数
      currentCurve[key] = value;
    } else if (key == "source" || key == "target" || key == "append" || key == "obj") {
      // 全局设置
      globalSettings[key] = value;
    }
  }
  
  // 添加最后一个曲线
  if (currentCurveIndex >= 0 && !currentCurve.isEmpty()) {
    m_curvesList.append(currentCurve);
    m_curveComboBox->addItem(curveName.isEmpty() ? tr("Curve #%1").arg(currentCurveIndex + 1) : curveName);
  }
  
  // 如果没有拿到任何曲线，添加一个空曲线
  if (m_curvesList.isEmpty()) {
    m_curvesList.append(QMap<QString, QString>());
    m_curveComboBox->addItem(tr("Curve #1"));
  }
  
  // 更新全局设置
  if (globalSettings.contains("source") && m_globalCommandEdits.contains("source")) {
    m_globalCommandEdits["source"]->setText(globalSettings["source"]);
  }
  
  if (globalSettings.contains("target") && m_globalCommandEdits.contains("target")) {
    m_globalCommandEdits["target"]->setText(globalSettings["target"]);
  }
  
  if (globalSettings.contains("append") && m_globalCommandCombos.contains("append")) {
    QComboBox* combo = m_globalCommandCombos["append"];
    for (int i = 0; i < combo->count(); i++) {
      if (combo->itemData(i).toString() == globalSettings["append"]) {
        combo->setCurrentIndex(i);
        break;
      }
    }
  }
  
  if (globalSettings.contains("obj") && m_globalCommandCombos.contains("obj")) {
    QComboBox* combo = m_globalCommandCombos["obj"];
    for (int i = 0; i < combo->count(); i++) {
      if (combo->itemData(i).toString() == globalSettings["obj"]) {
        combo->setCurrentIndex(i);
        break;
      }
    }
  }
  
  // 选择第一个曲线
  m_curveComboBox->setCurrentIndex(0);
  m_currentCurveIndex = 0;
  updateCurveEditor();
  
  QMessageBox::information(this, tr("Preset Loaded"), 
                         tr("Preset file successfully loaded with %1 curve(s).").arg(m_curvesList.size()));
}

void CurveGenInspector::curveSelectionChanged(int index)
{
  // 保存当前曲线数据
  if (m_currentCurveIndex >= 0 && m_currentCurveIndex < m_curvesList.size()) {
    QMap<QString, QString>& curveData = m_curvesList[m_currentCurveIndex];
    
    // 保存文本编辑框的值
    for (auto it = m_curveParamEdits.constBegin(); it != m_curveParamEdits.constEnd(); ++it) {
      QString value = it.value()->text().trimmed();
      if (!value.isEmpty()) {
        curveData[it.key()] = value;
      } else {
        curveData.remove(it.key());
      }
    }
    
    // 保存下拉框的值
    for (auto it = m_curveParamCombos.constBegin(); it != m_curveParamCombos.constEnd(); ++it) {
      QString value = it.value()->currentData().toString();
      if (!value.isEmpty()) {
        curveData[it.key()] = value;
      } else {
        curveData.remove(it.key());
      }
    }
  }
  
  // 更新当前索引
  m_currentCurveIndex = index;
  
  // 更新UI显示新选中曲线的数据
  updateCurveEditor();
}

void CurveGenInspector::updateCurveEditor()
{
  // Clear all fields first
  for (auto edit : m_curveParamEdits) {
    edit->clear();
  }
  
  for (auto combo : m_curveParamCombos) {
    combo->setCurrentIndex(0);
  }
  
  // If we have a valid selection, populate fields
  if (m_currentCurveIndex >= 0 && m_currentCurveIndex < m_curvesList.size()) {
    const QMap<QString, QString>& curveData = m_curvesList[m_currentCurveIndex];
    
    // Set line edit values
    for (auto it = curveData.constBegin(); it != curveData.constEnd(); ++it) {
      if (m_curveParamEdits.contains(it.key())) {
        m_curveParamEdits[it.key()]->setText(it.value());
      } else if (m_curveParamCombos.contains(it.key())) {
        // Find the index for this value
        QComboBox* combo = m_curveParamCombos[it.key()];
        for (int i = 0; i < combo->count(); i++) {
          if (combo->itemData(i).toString() == it.value()) {
            combo->setCurrentIndex(i);
            break;
          }
        }
      }
    }
  }
}

void CurveGenInspector::createToolExecutionPanel()
{
  // Main layout for the tool panel
  auto* mainLayout = new QVBoxLayout{m_toolPanel};
  mainLayout->setContentsMargins(10, 10, 10, 10);
  mainLayout->setSpacing(5);
  
  // Top controls area
  auto* controlsWidget = new QWidget{};
  auto* gridLayout = new QGridLayout{controlsWidget};
  gridLayout->setContentsMargins(0, 0, 0, 0);
  gridLayout->setSpacing(5);
  
  // Tool path selection
  auto* toolPathLabel = new QLabel{tr("Tool Path:")};
  m_toolPathEdit = new QLineEdit{};
  m_browseButton = new QPushButton{tr("Browse...")};
  
  // Parameter input
  auto* argsLabel = new QLabel{tr("Parameters:")};
  m_toolArgsEdit = new QLineEdit{};
  
  // Execute button
  m_executeButton = new QPushButton{tr("Execute")};
  
  // Terminate button
  m_terminateButton = new QPushButton{tr("Terminate")};
  m_terminateButton->setEnabled(false);  // Initially disabled
  m_terminateButton->setStyleSheet("QPushButton { color: red; font-weight: bold; }");
  
  // Add to layout
  gridLayout->addWidget(toolPathLabel, 0, 0);
  gridLayout->addWidget(m_toolPathEdit, 0, 1, 1, 2);
  gridLayout->addWidget(m_browseButton, 0, 3);
  
  gridLayout->addWidget(argsLabel, 1, 0);
  gridLayout->addWidget(m_toolArgsEdit, 1, 1, 1, 3);
  
  auto* buttonLayout = new QHBoxLayout{};
  buttonLayout->addStretch(1);
  buttonLayout->addWidget(m_executeButton);
  buttonLayout->addWidget(m_terminateButton);
  
  gridLayout->addLayout(buttonLayout, 2, 0, 1, 4);
  
  // Add controls widget to main layout
  mainLayout->addWidget(controlsWidget);
  
  // Tool output area (now integrated in the tool panel)
  mainLayout->addWidget(new QLabel(tr("Output:")));
  
  m_contentView = new QTextEdit{};
  m_contentView->setReadOnly(true);
  m_contentView->setPlaceholderText(tr("Tool execution output will be displayed here"));
  m_contentView->setMinimumHeight(200); // Set minimum height to ensure visibility
  
  mainLayout->addWidget(m_contentView, 1); // Give output area more stretch
  
  // Connect signals and slots
  connect(m_browseButton, &QPushButton::clicked, this, &CurveGenInspector::browseToolPath);
  connect(m_executeButton, &QPushButton::clicked, this, &CurveGenInspector::executeExternalTool);
  connect(m_terminateButton, &QPushButton::clicked, this, &CurveGenInspector::terminateExternalTool);
}

void CurveGenInspector::browseToolPath()
{
  QString filePath = QFileDialog::getOpenFileName(
    this, 
    tr("Select External Tool"), 
    QString{}, 
    tr("Executable Files (*.exe);;All Files (*.*)"));
    
  if (!filePath.isEmpty()) {
    m_toolPathEdit->setText(filePath);
  }
}

void CurveGenInspector::executeExternalTool()
{
  QString toolPath = m_toolPathEdit->text().trimmed();
  if (toolPath.isEmpty()) {
    QMessageBox::warning(this, tr("Error"), tr("请指定工具路径"));
    return;
  }
  
  // 确保输出区域可见
  m_contentView->show();
  
  // 如果已经有进程在运行，先清理
  if (m_process) {
    if (m_process->state() != QProcess::NotRunning) {
      m_process->kill();
      m_process->waitForFinished(500);
    }
    delete m_process;
    m_process = nullptr;
  }
  
  // 清空输出区域
  m_contentView->clear();
  
  // 获取工具路径目录
  QFileInfo toolFileInfo(toolPath);
  QString workDir = toolFileInfo.absolutePath();
  
  // 创建临时工作目录 - 使用工具所在目录而不是系统临时目录
  QString tempDirPath = workDir + "/TrenchBroom_Temp";
  QDir tempDir(tempDirPath);
  if (!tempDir.exists()) {
    if (!tempDir.mkpath(".")) {
      m_contentView->append(tr("[ERROR] Failed to create temp directory: %1").arg(tempDir.absolutePath()));
      return;
    }
    m_contentView->append(tr("Created temp directory: %1").arg(tempDir.absolutePath()));
  }
  
  // 获取当前选中内容并保存为map文件
  QString selectionContent = getCurrentSelectionContent();
  QString mapFilePath = tempDir.absolutePath() + "/temp_selection.map";
  QFile mapFile(mapFilePath);
  
  if (!mapFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    m_contentView->append(tr("[ERROR] Failed to create temporary map file"));
    return;
  }
  
  QTextStream mapStream(&mapFile);
  mapStream << selectionContent;
  mapFile.close();
  
  m_contentView->append(tr("Saved selection to: %1").arg(mapFilePath));
  
  // 保存当前编辑的曲线数据
  curveSelectionChanged(m_curveComboBox->currentIndex());
  
  // 生成预设文件路径
  QString presetFilePath = tempDir.absolutePath() + "/temp_preset.txt";
  
  // 构建预设文件内容
  QString presetContent = buildPresetFileContent();
  
  // 保存预设文件
  QFile presetFile(presetFilePath);
  if (!presetFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    m_contentView->append(tr("[ERROR] Failed to create preset file"));
    return;
  }
  
  QTextStream presetFileStream(&presetFile);
  presetFileStream << presetContent;
  presetFile.close();
  
  m_contentView->append(tr("Created preset file: %1").arg(presetFilePath));
  
  // 创建一个批处理文件来运行工具，避免"按任意键继续..."提示
  QString batchFilePath = tempDir.absolutePath() + "/run_tool.bat";
  QFile batchFile(batchFilePath);
  
  if (!batchFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    m_contentView->append(tr("[ERROR] Failed to create batch file"));
    return;
  }
  
  QTextStream batchStream(&batchFile);
  batchStream << "@echo off\n";
  batchStream << "cd /d \"" << workDir << "\"\n";
  batchStream << "\"" << toolPath << "\" \"" << presetFilePath << "\"\n";
  batchStream << "exit\n";
  batchFile.close();
  
  m_contentView->append(tr("Created batch file: %1").arg(batchFilePath));
  
  // 存储临时目录路径供后续清理使用
  m_lastTempDir = tempDirPath;
  
  // 显示执行信息
  m_contentView->append(tr("===== Starting Execution of %1 =====").arg(toolPath));
  m_contentView->append(tr("Arguments: %1").arg(presetFilePath));
  m_contentView->append(tr("Working Directory: %1").arg(workDir));
  
  // 更新按钮状态
  m_executeButton->setEnabled(false);
  m_terminateButton->setEnabled(true);
  
  // 创建进程
  m_process = new QProcess(this);
  m_process->setProcessChannelMode(QProcess::MergedChannels);
  
  // 连接信号
  connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          this, &CurveGenInspector::processFinished);
  connect(m_process, &QProcess::readyReadStandardOutput, 
          this, &CurveGenInspector::readProcessOutput);
  connect(m_process, &QProcess::readyReadStandardError, 
          this, &CurveGenInspector::readProcessOutput);
  connect(m_process, &QProcess::errorOccurred,
          this, &CurveGenInspector::processError);
  
  // 启动进程 - 执行批处理文件而不是直接执行工具
  try {
    m_process->start(batchFilePath);
    
    if (!m_process->waitForStarted(3000)) {
      m_contentView->append(tr("[ERROR] Process start timed out"));
      m_executeButton->setEnabled(true);
      m_terminateButton->setEnabled(false);
    }
  } catch (const std::exception& e) {
    m_contentView->append(tr("[CRITICAL ERROR] Process start exception: %1").arg(e.what()));
    m_executeButton->setEnabled(true);
    m_terminateButton->setEnabled(false);
  }
}

void CurveGenInspector::processFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
  m_contentView->append(tr("\n===== Process ended, exit code: %1 =====").arg(exitCode));
  
  if (exitStatus == QProcess::CrashExit) {
    m_contentView->append(tr("Process terminated abnormally"));
  }
  
  // 清理临时文件
  if (!m_lastTempDir.isEmpty()) {
    QDir tempDir(m_lastTempDir);
    
    // 清理可能存在的运行批处理文件
    QString batchFilePath = tempDir.absolutePath() + "/run_tool.bat";
    QFile batchFile(batchFilePath);
    if (batchFile.exists()) {
      if (batchFile.remove()) {
        m_contentView->append(tr("Cleaned up temporary batch file"));
      }
    }
    
    // 查找并删除较老的临时文件
    QStringList filters;
    filters << "*.bat" << "*.sh" << "temp_*.map" << "temp_preset.txt";
    QStringList oldFiles = tempDir.entryList(filters, QDir::Files);
    
    for (const QString& fileName : oldFiles) {
      // 确保文件不是我们刚刚创建的 - 只删除超过1小时的旧文件
      QFileInfo fileInfo(tempDir.absoluteFilePath(fileName));
      if (fileInfo.lastModified().secsTo(QDateTime::currentDateTime()) > 3600) {
        if (QFile::remove(tempDir.absoluteFilePath(fileName))) {
          m_contentView->append(tr("Cleaned up old temporary file: %1").arg(fileName));
        }
      }
    }
  }
  
  // 重置按钮状态
  m_executeButton->setEnabled(true);
  m_terminateButton->setEnabled(false);
}

void CurveGenInspector::readProcessOutput()
{
  if (!m_process) return;
  
  // Read standard output
  QByteArray outputData = m_process->readAllStandardOutput();
  if (!outputData.isEmpty()) {
    QString output = QString::fromLocal8Bit(outputData);
    m_contentView->append(output);
    
    // Check if output contains key prompt
    if (output.contains("Press any key") ||
        output.contains("press any key") ||
        output.contains("请按任意键继续")) {
      
      m_contentView->append(tr("Key prompt detected, sending Enter key..."));
      
      // Send Enter key
      m_process->write("\n");
    }
    
    // Ensure scrolling to bottom to show latest output
    QTextCursor cursor = m_contentView->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_contentView->setTextCursor(cursor);
    m_contentView->ensureCursorVisible();
  }
  
  // Read standard error
  QByteArray errorData = m_process->readAllStandardError();
  if (!errorData.isEmpty()) {
    QString errorOutput = tr("[ERROR] ") + QString::fromLocal8Bit(errorData);
    m_contentView->append(errorOutput);
    // Ensure scrolling to bottom to show latest output
    QTextCursor cursor = m_contentView->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_contentView->setTextCursor(cursor);
    m_contentView->ensureCursorVisible();
  }
  
  // Force event processing to ensure UI updates
  QApplication::processEvents();
}

void CurveGenInspector::processError(QProcess::ProcessError error)
{
  QString errorMessage;
  
  switch (error) {
    case QProcess::FailedToStart:
      errorMessage = tr("Failed to start process. Please check if the tool path is correct and if you have execution permissions.");
      break;
    case QProcess::Crashed:
      errorMessage = tr("Process crashed.");
      break;
    case QProcess::Timedout:
      errorMessage = tr("Process operation timed out.");
      break;
    case QProcess::WriteError:
      errorMessage = tr("Error writing data to process.");
      break;
    case QProcess::ReadError:
      errorMessage = tr("Error reading data from process.");
      break;
    default:
      errorMessage = tr("Unknown error.");
      break;
  }
  
  m_contentView->append(tr("\n[ERROR] ") + errorMessage);
  m_executeButton->setEnabled(true);
}

void CurveGenInspector::terminateExternalTool()
{
  if (!m_process || m_process->state() == QProcess::NotRunning) {
    m_terminateButton->setEnabled(false);
    return;
  }
  
  m_contentView->append(tr("\n===== Terminating process... ====="));
  
  // Record process ID for use if standard method fails
  qint64 pid = m_process->processId();
  
  // First try normal termination
  m_process->terminate();
  
  // Wait for process to end, wait at most 2 seconds
  if (!m_process->waitForFinished(2000)) {
    // If after 2 seconds the process is still running, force kill
    m_contentView->append(tr("[WARNING] Process not responding to termination request, forcing termination..."));
    m_process->kill();
    
    // Wait another second
    if (!m_process->waitForFinished(1000) && pid > 0) {
      // If still unable to terminate, try using system commands to force terminate
      m_contentView->append(tr("[WARNING] Process still not terminated, trying system commands to force terminate..."));
      
#ifdef Q_OS_WIN
      // Windows platform uses taskkill command
      QProcess::execute(QString("taskkill /F /PID %1").arg(pid));
#else
      // Unix platform uses kill command
      QProcess::execute(QString("kill -9 %1").arg(pid));
#endif
    }
  }
  
  m_contentView->append(tr("===== Process terminated ====="));
  
  // 清理临时文件
  if (!m_lastTempDir.isEmpty()) {
    QDir tempDir(m_lastTempDir);
    
    // 清理批处理文件
    QString batchFilePath = tempDir.absolutePath() + "/run_tool.bat";
    QFile batchFile(batchFilePath);
    if (batchFile.exists()) {
      if (batchFile.remove()) {
        m_contentView->append(tr("Cleaned up temporary batch file"));
      }
    }
  }
  
  // Delete process object and set to null to prevent subsequent access
  if (m_process) {
    m_process->disconnect(); // Disconnect all connections
    delete m_process;
    m_process = nullptr;
  }
  
  // Re-enable execute button, disable terminate button
  m_executeButton->setEnabled(true);
  m_terminateButton->setEnabled(false);
}

void CurveGenInspector::updateSelectionContent(const Selection& selection)
{
    auto doc = m_document.lock();
    if (!doc || !m_selectionView) return; // Prevent null pointer

    QString content;
    if (doc->hasSelectedNodes()) {
        content = QString::fromStdString(doc->serializeSelectedNodes());
    } else if (doc->hasSelectedBrushFaces()) {
        content = QString::fromStdString(doc->serializeSelectedBrushFaces());
    } else {
        content = tr("No selected content");
    }
    
    // Store the selection content for use with external tools
    m_currentSelectionContent = content;
    
    // Use separate text control to display selected content
    m_selectionView->setPlainText(content);
}

QString CurveGenInspector::getCurrentSelectionContent() const
{
    return m_currentSelectionContent;
}

QString CurveGenInspector::buildPresetFileContent() const
{
  QString content;
  QTextStream stream(&content);
  
  // 文件头
  stream << "// Preset file automatically generated by TrenchBroom\n\n";
  
  // 获取source和target路径
  QString sourcePath = "";
  if (m_globalCommandEdits.contains("source") && !m_globalCommandEdits["source"]->text().isEmpty()) {
    sourcePath = m_globalCommandEdits["source"]->text();
  } else {
    // 使用临时保存的选择内容路径
    // 获取工具路径，如果有效
    QString toolPath = m_toolPathEdit->text().trimmed();
    if (!toolPath.isEmpty()) {
      QFileInfo toolFileInfo(toolPath);
      QString workDir = toolFileInfo.absolutePath();
      QString mapFilePath = workDir + "/TrenchBroom_Temp/temp_selection.map";
      QFileInfo fileInfo(mapFilePath);
      if (fileInfo.exists()) {
        sourcePath = fileInfo.absoluteFilePath();
      }
    }
    
    // 如果仍然没有有效的源路径
    if (sourcePath.isEmpty()) {
      // 获取当前文档路径 - 如果有的话
      auto doc = m_document.lock();
      if (doc && !doc->path().empty()) {
        sourcePath = QString::fromStdString(doc->path().string());
      } else {
        sourcePath = "selection.map"; // 英文默认名称
      }
    }
  }
  
  QString targetPath = getTargetFilePath(sourcePath);
  
  // 写入全局设置
  stream << "source\t\t\"" << sourcePath << "\"\n";
  stream << "target\t\t\"" << targetPath << "\"\n";
  
  // 写入全局设置的其他选项（append和obj）
  if (m_globalCommandCombos.contains("append")) {
    stream << "append\t\t" << m_globalCommandCombos["append"]->currentData().toString() << "\n";
  } else {
    stream << "append\t\t0\n";  // 默认覆盖
  }
  
  if (m_globalCommandCombos.contains("obj")) {
    stream << "obj\t\t" << m_globalCommandCombos["obj"]->currentData().toString() << "\n";
  } else {
    stream << "obj\t\t0\n";  // 默认不导出OBJ
  }
  
  stream << "\n";
  
  // 遍历所有曲线
  for (int i = 0; i < m_curvesList.size(); ++i) {
    stream << "// ++++++++++ Curve Object #" << (i + 1) << " ++++++++++\n\n";
    
    const QMap<QString, QString>& curveData = m_curvesList[i];
    
    // 添加曲线名称
    stream << "name\t\t" << "Curve #" << (i + 1) << "\n";
    
    // 添加基本设置
    stream << "rad\t\t" << (curveData.contains("rad") ? curveData["rad"] : "0") << "\n";
    stream << "splinefile\t\"UNSET\"\n";
    stream << "nulltex\t\tNULL\n";
    
    // 添加曲线参数
    for (auto it = curveData.constBegin(); it != curveData.constEnd(); ++it) {
      if (it.key() != "rad") { // rad已经处理过
        stream << it.key() << "\t\t" << it.value() << "\n";
      }
    }
    
    // 为每一段曲线设置不同的范围
    if (!curveData.contains("range_start") && !curveData.contains("range_end")) {
      double segmentSize = 100.0 / m_curvesList.size();
      double start = i * segmentSize;
      double end = (i + 1) * segmentSize;
      
      stream << "range_start\t" << start << "\n";
      stream << "range_end\t" << end << "\n";
    }
    
    // 添加默认参数
    if (!curveData.contains("map")) stream << "map\t\t1\n";  // 默认生成map
    if (!curveData.contains("rmf")) stream << "rmf\t\t0\n";  // 默认不生成rmf
    if (!curveData.contains("spike_height")) stream << "spike_height\t4\n";
    if (!curveData.contains("type")) stream << "type\t\t0\n";
    if (!curveData.contains("shift")) stream << "shift\t\t5\n";
    if (!curveData.contains("tri")) stream << "tri\t\t1\n";
    if (!curveData.contains("round")) stream << "round\t\t0\n";
    if (!curveData.contains("ramp")) stream << "ramp\t\t0\n";
    if (!curveData.contains("bounds")) stream << "bounds\t\t1\n";
    if (!curveData.contains("transit_tri")) stream << "transit_tri\t0\n";
    if (!curveData.contains("skipnull")) stream << "skipnull\t0\n";
    
    // 其他高级参数
    stream << "rot\t\t\"0 0 0\"\n";
    stream << "rot_src\t\t\"0 0 0\"\n";
    stream << "move\t\t\"0 0 0\"\n";
    stream << "gridsize\t\"1 1 1\"\n";
    
    stream << "\n";
  }
  
  // 文件尾
  stream << "// End of preset file\n";
  
  return content;
}

QString CurveGenInspector::getTargetFilePath(const QString& sourcePath) const
{
  // 如果已经明确指定了target路径，直接使用
  if (m_globalCommandEdits.contains("target") && !m_globalCommandEdits["target"]->text().isEmpty()) {
    return m_globalCommandEdits["target"]->text();
  }
  
  // 从源路径生成目标路径
  QFileInfo fileInfo(sourcePath);
  QString baseName = fileInfo.completeBaseName();
  QString directory = fileInfo.absolutePath();
  
  return directory + "/" + baseName + "_curved.map";
}

CurveGenInspector::~CurveGenInspector()
{
  // Clean up process and timer
  if (m_process) {
    // First try normal closing
    if (m_process->state() != QProcess::NotRunning) {
      m_process->terminate();
      if (!m_process->waitForFinished(1000)) {
        // If unable to terminate normally, force terminate
        qint64 pid = m_process->processId();
        m_process->kill();
        
        // If still unable to terminate, use system commands
        if (!m_process->waitForFinished(500) && pid > 0) {
#ifdef Q_OS_WIN
          QProcess::execute(QString("taskkill /F /PID %1").arg(pid));
#else
          QProcess::execute(QString("kill -9 %1").arg(pid));
#endif
        }
      }
    }
    
    m_process->disconnect();
    delete m_process;
    m_process = nullptr;
  }
  
  saveWindowState(m_splitter);
}

} // namespace tb::ui
