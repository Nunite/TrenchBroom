//Added by lws

#pragma once

#include "ui/TabBook.h"

namespace tb::ui
{

/**
 * A widget that provides a Python console interface.
 */
class PythonConsole : public TabBookPage
{
  Q_OBJECT
public:
  /**
   * Creates a new Python console.
   *
   * @param parent the parent widget
   */
  explicit PythonConsole(QWidget* parent = nullptr);
  
  QWidget* createTabBarPage(QWidget* parent = nullptr) override;
};

} // namespace tb::ui
