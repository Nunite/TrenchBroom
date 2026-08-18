#pragma once

#include <memory>
#include <string>

namespace tb::ui
{
class MapDocument;

class PythonDocumentTransaction
{
private:
  MapDocument* m_document = nullptr;
  struct Impl;
  std::unique_ptr<Impl> m_impl;

public:
  explicit PythonDocumentTransaction(
    MapDocument& document, std::string name = "Python Console Command");
  ~PythonDocumentTransaction();

  PythonDocumentTransaction(const PythonDocumentTransaction&) = delete;
  PythonDocumentTransaction& operator=(const PythonDocumentTransaction&) = delete;
  PythonDocumentTransaction(PythonDocumentTransaction&&) noexcept;
  PythonDocumentTransaction& operator=(PythonDocumentTransaction&&) noexcept;

  bool commit();
  void cancel();
};

bool installPythonV2Module();

} // namespace tb::ui
