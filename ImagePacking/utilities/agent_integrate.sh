
echo "Image = $1 New Version = $2"

image=$1

release=$(echo $2 | cut -d'.' -f1)
version=$(echo $2 | cut -d'.' -f2)
maintenance=$(echo $2 | cut -d'.' -f3)
build=$(echo $2 | cut -d'.' -f4)

./tools/agent_stk -d -r $release -v $version -m $maintenance -b $build $image
