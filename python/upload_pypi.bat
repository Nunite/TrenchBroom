@echo off
setlocal
cd /d "%~dp0"

python -m pip install -U build twine
python -m build

if "%TWINE_USERNAME%"=="" (
  echo TWINE_USERNAME is not set. Use "__token__".
  exit /b 1
)

if "%TWINE_PASSWORD%"=="" (
  echo TWINE_PASSWORD is not set. Use your PyPI token.
  exit /b 1
)

python -m twine upload dist/*
