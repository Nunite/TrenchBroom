//Added by lws

#include "PythonConsole.h"

#include <QVBoxLayout>
#include <QLabel>

namespace tb::ui
{

PythonConsole::PythonConsole(QWidget* parent)
  : TabBookPage{parent}
{
  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  
  auto* placeholder = new QLabel{tr("Python Console - Placeholder")};
  placeholder->setAlignment(Qt::AlignCenter);
  
  layout->addWidget(placeholder);
  setLayout(layout);
}

QWidget* PythonConsole::createTabBarPage(QWidget* parent)
{
  return new QLabel{tr("Python Console"), parent};
}

} // namespace tb::ui
