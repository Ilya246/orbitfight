if [[ $WININCLUDE == "" || $WINLIB == "" ]]; then
	echo "Define \$WININCLUDE and \$WINLIB as the directories with Windows includes and libraries.";
	exit;
fi;
CXX=x86_64-w64-mingw32-g++-posix CXXFLAGS="-O3 -Wall -Wextra -pedantic -I${WININCLUDE} -DSFML_STATIC -static" LDFLAGS="-L${WINLIB} -lsfml-graphics-s -lsfml-window-s -lsfml-network-s -lsfml-system-s -l winmm -l opengl32 -l gdi32 -l freetype -l ws2_32 -static" make -j
