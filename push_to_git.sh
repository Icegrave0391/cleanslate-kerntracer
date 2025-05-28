#!/bin/bash

branch="wensan_3"
url="git@github.com:Icegrave0391/cleanslate-kerntracer.git"

if [ ! -d ".git" ]; then
    git init
fi

git fetch origin $branch 2>/dev/null

if git show-ref --verify --quiet refs/heads/$branch; then
    git checkout $branch
    git pull origin $branch --rebase
else
    if git ls-remote --exit-code --heads origin $branch; then
        git checkout -b $branch origin/$branch
        git pull origin $branch --rebase
    else
        git checkout -b $branch
    fi
fi

git add .
git commit -m "What I did was commiting some changes." || echo "No changes to commit."

if ! git remote get-url origin >/dev/null 2>&1; then
    git remote add origin $url
fi

git push -u origin $branch --force

echo "Pushed to branch $branch at $url"
