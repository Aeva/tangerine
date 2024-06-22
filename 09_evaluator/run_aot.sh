#!/usr/bin/env sh
dotnet publish -r linux-x64 -c Release
./bin/Release/net8.0/linux-x64/publish/Evaluator
