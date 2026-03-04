image=$1
signing_dir=$2
private_key=ng_privatekey.pem
sign_hdr=ng_signheader.txt
sign_footer=ng_signfooter.txt

echo " ReSigning the image"
openssl dgst -hex -sha256 -sign $signing_dir/$private_key -out image.dgst_256 $image
cat image.dgst_256 | cut -d' ' -f2 > image.dgst
base64 image.dgst > signature_base64
cat $signing_dir/$sign_hdr signature_base64 $signing_dir/$sign_footer >> $image
echo "Signing Complete and image is $image"
