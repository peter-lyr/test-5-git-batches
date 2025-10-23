@echo off
cd %~dp0
split-large-files-in-git-repo.py
git-commit-large-files.py commit-info.txt
