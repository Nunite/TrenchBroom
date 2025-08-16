//Added by Lws

#pragma once

#include "ui/PreferencePane.h"

class QButtonGroup;
class QRadioButton;

namespace tb::ui
{

class LanguagePreferencePane : public PreferencePane
{
  Q_OBJECT
private:
  QRadioButton* m_englishRadioButton;
  QRadioButton* m_chineseRadioButton;
  QButtonGroup* m_languageButtonGroup;

signals:
  void languageChanged();

public:
  explicit LanguagePreferencePane(QWidget* parent = nullptr);

private:
  void createGui();
  bool canResetToDefaults() override;
  void doResetToDefaults() override;
  void updateControls() override;
  void showRestartRequiredMessage();
  bool validate() override;
};

} // namespace tb::ui