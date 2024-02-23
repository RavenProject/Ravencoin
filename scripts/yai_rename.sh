#!/bin/bash

find -type f -not -path "./.git/*" -and -not -path "./scripts/*"  -exec sed -i 's/ravencoin/yottaflux/g' {} +
find -type f -not -path "./.git/*" -and -not -path "./scripts/*" -exec sed -i 's/Ravencoin/Yottaflux/g' {} +
find -type f -not -path "./.git/*" -and -not -path "./scripts/*" -exec sed -i 's/RAVENCOIN/YOTTAFLUX/g' {} +

find -type f -not -path "./.git/*" -and -not -path "./scripts/*" -exec sed -i 's/RavenProject/yottaflux/g' {} +


find -type f -not -path "./.git/*" -and -not -path "./scripts/*"  -exec sed -i 's/rvn/yai/g' {} +
find -type f -not -path "./.git/*" -and -not -path "./scripts/*"  -exec sed -i 's/RVN/YAI/g' {} +

find -type f -not -path "./.git/*" -and -not -path "./scripts/*"  -exec sed -i 's/Raven/Yottaflux/g' {} +
find -type f -not -path "./.git/*" -and -not -path "./scripts/*"  -exec sed -i 's/raven/yottaflux/g' {} +

find -type f -not -path "./.git/*" -and -not -path "./scripts/*" -exec sed -i 's/yottaflux\/rips/RavenProject\/rips/g' {} +
find -type f -not -path "./.git/*" -and -not -path "./scripts/*" -exec sed -i 's/yottaflux\.org/yottaflux\.ai/ig' {} +
find -type f -not -path "./.git/*" -and -not -path "./scripts/*" -exec sed -i 's/Yottaflux Core developers/Ravencoin Core developers/g' {} +
