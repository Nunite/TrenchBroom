@echo off
setlocal
cd /d "%~dp0"

python -m pip install -U build twine
if exist dist rmdir /s /q dist
python -m build

python -m twine upload --skip-existing dist/*
