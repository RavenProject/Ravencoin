#!/bin/bash

# Ports

find -type f -not -path "./.git/*" -and -not -path "./scripts/*"  -exec sed -i 's/8766/8558/g' {} +
find -type f -not -path "./.git/*" -and -not -path "./scripts/*"  -exec sed -i 's/8767/8559/g' {} +
find -type f -not -path "./.git/*" -and -not -path "./scripts/*"  -exec sed -i 's/18766/18558/g' {} +
find -type f -not -path "./.git/*" -and -not -path "./scripts/*"  -exec sed -i 's/18767/18559/g' {} +
find -type f -not -path "./.git/*" -and -not -path "./scripts/*"  -exec sed -i 's/18443/18561/g' {} +
find -type f -not -path "./.git/*" -and -not -path "./scripts/*"  -exec sed -i 's/18444/18560/g' {} +

exit


find -type f -not -path "./.git/*" -and -not -path "./scripts/*"  -exec sed -i 's/ravencoin/yottaflux/g' {} +
find -type f -not -path "./.git/*" -and -not -path "./scripts/*" -exec sed -i 's/Ravencoin/Yottaflux/g' {} +
find -type f -not -path "./.git/*" -and -not -path "./scripts/*" -exec sed -i 's/RAVENCOIN/YOTTAFLUX/g' {} +

find -type f -not -path "./.git/*" -and -not -path "./scripts/*" -exec sed -i 's/RavenProject/yottaflux/g' {} +


find -type f -not -path "./.git/*" -and -not -path "./scripts/*"  -exec sed -i 's/rvn/yai/g' {} +
find -type f -not -path "./.git/*" -and -not -path "./scripts/*"  -exec sed -i 's/RVN/YAI/g' {} +

find -type f -not -path "./.git/*" -and -not -path "./scripts/*"  -exec sed -i 's/Raven/Yottaflux/g' {} +
find -type f -not -path "./.git/*" -and -not -path "./scripts/*"  -exec sed -i 's/raven/yottaflux/g' {} +
find -type f -not -path "./.git/*" -and -not -path "./scripts/*"  -exec sed -i 's/RAVEN/YOTTAFLUX/g' {} +

find -type f -not -path "./.git/*" -and -not -path "./scripts/*" -exec sed -i 's/yottaflux\/rips/RavenProject\/rips/g' {} +
find -type f -not -path "./.git/*" -and -not -path "./scripts/*" -exec sed -i 's/yottaflux\.org/yottaflux\.ai/ig' {} +
find -type f -not -path "./.git/*" -and -not -path "./scripts/*" -exec sed -i 's/Yottaflux Core developers/Ravencoin Core developers/g' {} +
find -type f -not -path "./.git/*" -and -not -path "./scripts/*" -exec sed -i 's/Ravencoin Core/Raven Core/g' {} +
find -type f -not -path "./.git/*" -and -not -path "./scripts/*" -exec sed -i 's/Yottaflux Developers/Raven Developers/g' {} +


find -type f -not -path "./.git/*" -and -not -path "./scripts/*" -exec sed -i 's/Yottaflux Core/Yottaflux/g' {} +
find -type f -not -path "./.git/*" -and -not -path "./scripts/*" -exec sed -i 's/YottafluxCore/Yottaflux/g' {} +

find . -name "*ravencoin*" -exec rename 's/ravencoin/yottaflux/' '{}' \; 
find . -name "*raven*" -exec rename 's/raven/yottaflux/' '{}' \; 

