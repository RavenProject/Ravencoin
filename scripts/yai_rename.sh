#!/bin/bash

find -type f -not -path "./.git/*" -exec sed -i 's/yottaflux/yottaflux/g' {} +
find -type f -not -path "./.git/*" -exec sed -i 's/Yottaflux/Yottaflux/g' {} +
find -type f -not -path "./.git/*" -exec sed -i 's/YOTTAFLUX/YOTTAFLUX/g' {} +

find -type f -not -path "./.git/*" -exec sed -i 's/yottaflux/yottaflux/g' {} +

find -type f -not -path "./.git/*" -exec sed -i 's/Ravencoin Core developers/Ravencoin Core developers/g' {} +




