despath=$1
if test "$despath"==""; then 
	despath="../wtpy"
fi

echo "wtpy path is $despath"

# if macosx64 else macosarm64
#arch_mode = `uname -m`


root="./src/build_all/build_x64/Release/bin"
folders=("Loader" "WtBtPorter" "WtDtPorter" "WtPorter")
for folder in ${folders[@]}
do
	cp -rvf $root/$folder/*.dylib $despath/wtpy/wrapper/macos_arm64
	for file in `ls $root/$folder`
	do
		if [ -d $root"/"$folder"/"$file ]
		then
			cp -rvf $root/$folder/$file/*.dylib $despath/wtpy/wrapper/macos_arm64/$file
		fi
	done
done