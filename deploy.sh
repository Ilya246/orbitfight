mkdir -p deploy;

cp build-win/orbitfight.exe deploy;
strip deploy/orbitfight.exe;
zip deploy/orbitfight_win64.zip deploy/orbitfight.exe;
echo "Packed orbitfight_win64.zip."

cp build/orbitfight deploy/orbitfight;
strip deploy/orbitfight;
tar -czvf deploy/orbitfight_linux64.tar.gz deploy/orbitfight;
echo "Packed orbitfight_linux64.zip."
