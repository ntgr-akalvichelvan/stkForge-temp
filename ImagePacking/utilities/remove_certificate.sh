
IMAGE=$1

size=$(ls -l $IMAGE | cut -d' ' -f5)
new_size=$((size-884))

echo "Size of Image: $IMAGE with vs without certificate = $size vs $new_size"
strip_file=$(echo $IMAGE | cut -d'.' -f1-4)


new_file="${strip_file}-strip.stk"
echo "Removing the certificate from the signed image"
dd if=$IMAGE of=$new_file bs=1 count=$new_size
echo result = $?


