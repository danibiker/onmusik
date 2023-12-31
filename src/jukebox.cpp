#include "jukebox.h"

const string filterExt = ".ogg .mp3";
const string musicDir = "Music";
const string fileMetadata = "metadata.txt";

bool Jukebox::canPlay;

/**
*
*/
Jukebox::Jukebox(){

    size_t pos = Constant::getAppDir().rfind(Constant::getFileSep());
    if (pos == string::npos){
        FILE_SEPARATOR = FILE_SEPARATOR_UNIX;
        tempFileSep[0] = FILE_SEPARATOR;
    }

    convertedFilesList = new listaSimple<string>();
    rutaInfoId3 = "";
    canPlay = false;
//    concatNameFolder = true;

    this->serverDownloader = NULL;
    arrCloud[DROPBOXSERVER] = new Dropbox();
    arrCloud[GOOGLEDRIVESERVER] = new GoogleDrive();
    arrCloud[ONEDRIVESERVER] = new Onedrive();
    serverSelected = DROPBOXSERVER;
    aborted = false;
    cdDrive = "";
    extractionPath = "";
    filterUploadExt = ".ogg";
    fileDownloaded = true;
    serversAuthenticated = false;
    albumsAndPlaylistsObtained = false;
    cddbObtained = false;
}

/**
*
*/
Jukebox::~Jukebox()
{
    string rutaMetadata = Constant::getAppDir() + FILE_SEPARATOR + fileMetadata;
    string tempFileDir = Constant::getAppDir() + FILE_SEPARATOR + "temp.ogg";

    Dirutil dir;
    dir.borrarArchivo(rutaMetadata);
    dir.borrarArchivo(tempFileDir);

    Traza::print("Eliminando servidores", W_DEBUG);
    for (int i=0; i < MAXSERVERS; i++){
        delete arrCloud[i];
    }
    Traza::print("Servidores eliminados", W_DEBUG);
}

/**
*
*/
uint32_t Jukebox::convertAndUpload(){
    convertir(dirToUpload, NULL);
    generarMetadatos(dirToUpload);
    uploadMusicToServer(dirToUpload);
    return 0;
}

/**
*
*/
uint32_t Jukebox::upload(){
    generarMetadatos(dirToUpload);
    uploadMusicToServer(dirToUpload);
    return 0;
}

/**
*
*/
uint32_t Jukebox::downloadFile(){
    downloadFile(fileToDownload);
    return 0;
}



/**
*
*/
uint32_t Jukebox::extraerCD(){
    if (!cdDrive.empty() && !extractionPath.empty()){
        CdTrackInfo cddbTrack;
        int res = extraerCD(cdDrive, extractionPath, &cddbTrack);

        if (res > 0 && !extractionPath.empty()){
            string ruta = extractionPath + FILE_SEPARATOR + "rip";
            Dirutil dir;

            listaSimple<FileProps> *filelist = new listaSimple<FileProps>();
            dir.listarDir(ruta.c_str(), filelist);

            if (filelist->getSize() > 0){
                unsigned int i=0;
                bool dirFound = false;
                string rutaRip;
                while (i < filelist->getSize() && !dirFound){
                    if (filelist->get(i).filetype == TIPODIRECTORIO && filelist->get(i).filename.compare(".") != 0
                        && filelist->get(i).filename.compare("..") != 0)
                    {
                            dirFound = true;
                            rutaRip = filelist->get(i).dir + FILE_SEPARATOR + filelist->get(i).filename;
                    } else {
                        i++;
                    }
                }

                Traza::print("Jukebox::extraerCD. Subiendo desde: " + rutaRip, W_DEBUG);
                setDirToUpload(rutaRip);
                convertir(rutaRip, &cddbTrack);
                uploadMusicToServer(rutaRip);
                if (dirFound && !rutaRip.empty()){
                    dir.borrarDir(rutaRip);
                }
            }
            delete filelist;
        }
    }

    return 0;
}

/**
*
*/
uint32_t Jukebox::refreshAlbumAndPlaylist(){
    aborted = false;
    setAlbumsAndPlaylistsObtained(false);
    //Refresco de la lista de albumes
    refreshAlbum();

    UITreeListBox *albumList = ((UITreeListBox *)ObjectsMenu->getObjByName("albumList"));
    if (albumList->getSize() > 0){
        this->setAlbumSelected(albumList->get(0).value);
        this->setServerSelected(Constant::strToTipo<int>(albumList->get(0).dest));
        refreshPlaylist();
    }
    setAlbumsAndPlaylistsObtained(true);
    return 0;
}

/**
*
*/
uint32_t Jukebox::refreshPlaylist(){
    Traza::print("Jukebox::refreshPlaylist", W_DEBUG);
    setAlbumsAndPlaylistsObtained(false);
    Dirutil dir;
    CloudFiles files;
    string ruta = "";
    map<string, string> metadatos;
    string strSeconds = "";
    unsigned long ulongSeconds = 0;
    string metaKeyTime = "";
    string metaKeyArtist = "";
    string metaKeyAlbum = "";
    string metaKeyTitle = "";
    string fichero = "";
    string metadataCloud = "";
    string idFile = "";
    string filename = "";
    UIListGroup *playList = ((UIListGroup *)ObjectsMenu->getObjByName("playLists"));
    UITreeListBox *albumList = ((UITreeListBox *)ObjectsMenu->getObjByName("albumList"));

    TreeNode parentNode = albumList->get(albumList->getLastSelectedPos());

    IOauth2 *server = this->getServerCloud(this->getServerSelected());
    bool needAlbumRefresh = false;
    bool needListRefresh = true;

    if (!this->albumSelected.empty()){
        string albumSelec = this->albumSelected;
        Traza::print("albumSelec: " + albumSelec, W_DEBUG);
        //Obtenemos los ficheros que nos devuelve dropbox
        Traza::print("Jukebox::refreshPlaylist. ListFiles: " + albumSelec, W_DEBUG);
        server->listFiles(albumSelec, server->getAccessToken(), &files);
        string metadataLocal = Constant::getAppDir() + FILE_SEPARATOR + "metadata.txt";
        Traza::print("Jukebox::refreshPlaylist. Descargando Metadatos: " + metadataLocal, W_DEBUG);
        //Descargando fichero con metadatos

        if (!aborted){
            if (this->getServerSelected() == DROPBOXSERVER){
                metadataCloud = albumSelec + "/" + "metadata.txt";
            } else {
                metadataCloud = server->fileExist("metadata.txt",albumSelec, server->getAccessToken());
            }
            Traza::print("Jukebox::refreshPlaylist. Descargando metadatos: " + metadataCloud, W_DEBUG);
            bool metaOK = server->getFile(metadataLocal,metadataCloud,server->getAccessToken()) == 0;

            if (metaOK){
                hashMapMetadatos(&metadatos, metadataLocal);
                Traza::print("Jukebox::refreshPlaylist. Generando Hashmap", W_DEBUG);
            }

            Traza::print("Jukebox::refreshPlaylist. Rellenando metadatos para cada cancion", W_DEBUG);
            string tmpFich = "";

            for (unsigned int i=0; i < files.fileList.size(); i++){
                ruta = files.fileList.at(i)->path;
                filename = ruta.substr(ruta.find_last_of("/")+1);
                idFile = files.fileList.at(i)->strHash;
                Constant::lowerCase(&filename);

                if (files.fileList.at(i)->isDir && parentNode.estado != TREENODEOBTAINED){
                    albumList->add(parentNode.id + "." +  Constant::TipoToStr(i), filename, idFile, music, this->getServerSelected(), true);
                    needAlbumRefresh = true;
                } else if (!files.fileList.at(i)->isDir && filterExt.find(dir.getExtension(filename)) != string::npos){
                    fichero = dir.getFileNameNoExt(filename);
                    //Remove special chars
                    tmpFich = Constant::toAlphanumeric(fichero);
                    //and make it lower case
                    Constant::lowerCase(&tmpFich);
                    //Traza::print("tmpFich: " + tmpFich, W_DEBUG);
                    if (needListRefresh){
                        playList->clearLista();
                        playList->setPosActualLista(0);
                        playList->refreshLastSelectedPos();
                        needListRefresh = false;
                    }
                    //Miramos en la hashmap si existe nuestra clave
                    if (metaOK){
                        metaKeyTime = tmpFich + arrTags[tagDuration];
                        if (metadatos.count(metaKeyTime) > 0){
                            //cout << "Buscando tiempo para: " << metaKeyTime << endl;
                            strSeconds = metadatos.at(metaKeyTime);
                            ulongSeconds = floor(Constant::strToTipo<double>(strSeconds));
                        } else {
                            strSeconds = "";
                            ulongSeconds = 0;
                        }

                        metaKeyArtist = getMetadatos(&metadatos, tmpFich + arrTags[tagArtist]);
                        metaKeyAlbum = getMetadatos(&metadatos, tmpFich + arrTags[tagAlbum]);
                        metaKeyTitle = getMetadatos(&metadatos, tmpFich + arrTags[tagTitle]);
    //                    Traza::print("Jukebox::refreshPlaylist. Album: " + metaKeyAlbum, W_DEBUG);
    //                    Traza::print("Jukebox::refreshPlaylist. Title: " + metaKeyTitle, W_DEBUG);
                        metaKeyAlbum = Constant::udecodeUTF8(metaKeyAlbum);
                        metaKeyTitle = Constant::udecodeUTF8(metaKeyTitle);
                        metaKeyArtist = Constant::udecodeUTF8(metaKeyArtist);
    //                    Traza::print("Jukebox::refreshPlaylist. Album: " + metaKeyAlbum, W_DEBUG);
    //                    Traza::print("Jukebox::refreshPlaylist. Title: " + metaKeyTitle, W_DEBUG);
                    }
                    vector <ListGroupCol *> miFila;
                    miFila.push_back(new ListGroupCol(metaKeyTitle.empty() ? fichero : metaKeyTitle, idFile));
                    miFila.push_back(new ListGroupCol(metaKeyArtist, metaKeyArtist));
                    miFila.push_back(new ListGroupCol(metaKeyAlbum, metaKeyAlbum));
                    miFila.push_back(new ListGroupCol(Constant::timeFormat(ulongSeconds), strSeconds));
                    playList->addElemLista(miFila);
                }
            }
        }
        playList->setImgDrawed(false);
        if (needAlbumRefresh){
            parentNode.estado = TREENODEOBTAINED;
            parentNode.ico = folder;
            albumList->refreshNode(parentNode);
            albumList->sort();
            albumList->calcularScrPos();
            albumList->setImgDrawed(false);
        }
    }
    Traza::print("Jukebox::refreshPlaylist END", W_DEBUG);
    setAlbumsAndPlaylistsObtained(true);

    return 0;
}

/**
* Convierte al formato ogg todos los ficheros contenidos en el directorio
* especificado por parametro. Tambien genera el fichero de metadatos con la informacion
* de cada cancion
*/
void Jukebox::convertir(string ruta, CdTrackInfo *cddbTrack){
    Dirutil dir;
    vector <FileProps> *filelist = new vector <FileProps>();
    Traza::print("Jukebox::convertir", W_DEBUG);
    Transcode trans;
    struct AudioParams audio_dst;
    audio_dst.channels = 2;
    audio_dst.bitrate = 128000;
    trans.setAudio_dst(audio_dst);
    FileProps file;
    int countFile = 0;
    //wbuilder.settings_["indentation"] = "";
    string rutaFicheroOgg;

    try{
        convertedFilesList->clear();
        Traza::print("Jukebox::clear", W_DEBUG);
        dir.listarDirRecursivo(ruta, filelist, filtroFicheros);
        string rutaMetadata = ruta + FILE_SEPARATOR + fileMetadata;
        dir.borrarArchivo(rutaMetadata);
        Traza::print("Codificando " + Constant::TipoToStr(filelist->size()) + " archivos de la ruta " + ruta, W_DEBUG);

        for (unsigned int i=0; i < filelist->size(); i++){
            file = filelist->at(i);
            if (filtroFicheros.find(dir.getExtension(file.filename)) != string::npos && file.filename.compare("..") != 0){
                rutaFicheroOgg = file.dir + FILE_SEPARATOR + dir.getFileNameNoExt(file.filename) + ".ogg";
                //Counter for the codification progress message
                countFile++;
                int percent = (countFile/(float)(filelist->size()))*100;
                ObjectsMenu->getObjByName("statusMessage")->setLabel(Constant::toAnsiString("Recodificando "
                        + Constant::TipoToStr(percent) + "% " + " " + file.filename));

                //Si no existe fichero ogg, debemos recodificar
                if (!dir.existe(rutaFicheroOgg)){
                    //Si tenemos informacion del cd, generamos sus id3_tags
                    TID3Tags *tag = NULL;
                    if (cddbTrack != NULL && i < cddbTrack->titleList.size() ){
                        size_t pos;
                        tag = new TID3Tags();
                        tag->album = cddbTrack->albumName;
                        tag->artist = cddbTrack->albumName;
                        if ((pos = cddbTrack->albumName.find("/")) != string::npos ){
                            tag->artist = cddbTrack->albumName.substr(0, pos);
                            tag->album = cddbTrack->albumName.substr(pos + 1);
                        }
                        tag->title = cddbTrack->titleList.at(i);
                        tag->date = cddbTrack->year;
                        tag->genre = cddbTrack->genre;
                        tag->track = Constant::TipoToStr(i+1) + "/" + Constant::TipoToStr(cddbTrack->titleList.size());
                    }
                    trans.transcode(file.dir + FILE_SEPARATOR + file.filename, rutaFicheroOgg, tag);
                    if (tag != NULL) delete tag;
                    //Anyadimos el fichero convertido
                    convertedFilesList->add(rutaFicheroOgg);
                } else {
                    //Anyadimos el fichero convertido
                    convertedFilesList->add(rutaFicheroOgg);
                }
                Traza::print("Conversion terminada: " + file.filename, W_DEBUG);
            }
        }
        convertedFilesList->sort();
    } catch (Excepcion &e){
        Traza::print("Error Jukebox::convertir" + string(e.getMessage()), W_ERROR);
    }
}

/**
 *
 * @param ruta
 */
void Jukebox::generarMetadatos(string ruta){
    Dirutil dir;
    vector <FileProps> *filelist = new vector <FileProps>();
    Traza::print("Jukebox::generarMetadatos", W_DEBUG);
    Transcode trans;
    FileProps file;
    Fileio fichero;
    Json::Value root;   // starts as "null"; will contain the root value after parsing
    string songFileName = "cancion";
    Json::StreamWriterBuilder wbuilder;
    TID3Tags id3Tags;

    try{
        dir.listarDirRecursivo(ruta, filelist, filterUploadExt);
        Traza::print("Metadatos para " + Constant::TipoToStr(filelist->size()) + " archivos de la ruta " + ruta, W_DEBUG);
        string lastDir, rutaMetadata;

        for (unsigned int i=0; i < filelist->size(); i++){
            file = filelist->at(i);
            if (filterUploadExt.find(dir.getExtension(file.filename)) != string::npos && file.filename.compare("..") != 0){
                if (lastDir.empty()){
                    lastDir = file.dir;
                } else if (lastDir.compare(file.dir) != 0 //Directory changed
                        ){
                    //Finalmente escribimos el fichero de metadata
                    rutaMetadata = lastDir + FILE_SEPARATOR + fileMetadata;
                    dir.borrarArchivo(rutaMetadata);
                    std::string outputConfig = Json::writeString(wbuilder, root);
                    fichero.writeToFile(rutaMetadata.c_str(), (char *)outputConfig.c_str(), outputConfig.length(), true);
                    lastDir = file.dir;
                    root.clear();
                }

                id3Tags = trans.getSongInfo(file.dir + FILE_SEPARATOR + file.filename);
                Traza::print("Buscando Metadatos para: " + file.dir + FILE_SEPARATOR + file.filename, W_DEBUG);
                //Generating metadata with time info for each song
                songFileName = Constant::uencodeUTF8(dir.getFileNameNoExt(file.filename));
                root[songFileName]["album"] = Constant::uencodeUTF8(id3Tags.album);
                root[songFileName]["title"] = Constant::uencodeUTF8(id3Tags.title);
                root[songFileName]["duration"] = id3Tags.duration;
                root[songFileName]["track"] = id3Tags.track;
                root[songFileName]["genre"] = id3Tags.genre;
                root[songFileName]["publisher"] = id3Tags.publisher;
                root[songFileName]["composer"] = id3Tags.composer;
                root[songFileName]["artist"] = id3Tags.artist;

                if (i + 1 == filelist->size()){ //Same Directory but last file
                    //Finalmente escribimos el fichero de metadata
                    rutaMetadata = lastDir + FILE_SEPARATOR + fileMetadata;
                    dir.borrarArchivo(rutaMetadata);
                    std::string outputConfig = Json::writeString(wbuilder, root);
                    fichero.writeToFile(rutaMetadata.c_str(), (char *)outputConfig.c_str(), outputConfig.length(), true);
                }
            }
        }

    } catch (Excepcion &e){
        Traza::print("Error Jukebox::generarMetadatos" + string(e.getMessage()), W_ERROR);
    }
}
/**
*
*/
string Jukebox::getMetadatos(map<string, string> *metadatos, string key){
    if (metadatos->count(key) > 0){
        return metadatos->at(key);
    } else {
        return "";
    }
}

/**
*
*/
void Jukebox::hashMapMetadatos(map<string, string> *metadatos, string ruta){
     Traza::print("Jukebox::hashMapMetadatos", W_DEBUG);
     try{
        Json::Value root;   // will contains the root value after parsing.
        Json::Reader reader;
        Fileio file;
        Traza::print("Jukebox::hashMapMetadatos. Cargando de ruta: " + ruta, W_DEBUG);
        file.loadFromFile(ruta);
        Traza::print("Jukebox::hashMapMetadatos. Parseando...", W_DEBUG);
        bool parsingSuccessful = reader.parse( file.getFile(), root );

        if ( !parsingSuccessful ){
            Traza::print("Jukebox::hashMapMetadatos. Failed to parse configuration: " + reader.getFormattedErrorMessages(), W_ERROR);
        } else {
            string songFileName;
            string atributeName;
            string atributeValue;
            Traza::print("Jukebox::hashMapMetadatos. Buscando atributos...", W_DEBUG);
            for (unsigned int i=0; i < root.getMemberNames().size(); i++){
                songFileName = root.getMemberNames().at(i);
                Traza::print("Jukebox::hashMapMetadatos. cancion: " + songFileName, W_DEBUG);
                if (songFileName.compare("error") != 0){
                    //We decode it from utf8
                    songFileName = Constant::udecodeUTF8(songFileName);
                    //Remove special chars
                    songFileName = Constant::toAlphanumeric(songFileName);
                    //and make it lower case
                    Constant::lowerCase(&songFileName);

                    Traza::print("Jukebox::hashMapMetadatos. decodificada: " + songFileName, W_DEBUG);
                    Json::Value value;
                    value = root[root.getMemberNames().at(i)];
                    for (unsigned int j=0; j < value.getMemberNames().size(); j++){
                        atributeName = value.getMemberNames().at(j);
                        atributeValue = value[atributeName].asString();
                        metadatos->insert( make_pair(songFileName + atributeName, atributeValue));
                        Traza::print(songFileName + atributeName + ": " + atributeValue, W_DEBUG);
                    }
                }
            }
            Traza::print("Jukebox::hashMapMetadatos. END", W_DEBUG);
        }
    }catch (Excepcion &e){
        Traza::print("No se ha podido obtener el fichero de metadatos", W_ERROR);
    }
}

/**
*
*/
uint32_t Jukebox::refreshPlayListMetadata(){
    UIListGroup *playList = ((UIListGroup *)ObjectsMenu->getObjByName("playLists"));
    string file = Constant::getAppDir() + FILE_SEPARATOR + "temp.ogg";
    unsigned int posSongSelected = playList->getLastSelectedPos();
    Traza::print("Obteniendo datos de la cancion: " + file, W_DEBUG);
    Transcode trans;
    TID3Tags songTags = trans.getSongInfo(file);
    Traza::print("Datos obtenidos", W_DEBUG);
    if (!songTags.title.empty()) playList->getCol(posSongSelected, 0)->setTexto(Constant::toAnsiString(songTags.title));
    if (!songTags.artist.empty()) playList->getCol(posSongSelected, 1)->setValor(songTags.artist);
    if (!songTags.artist.empty()) playList->getCol(posSongSelected, 1)->setTexto(Constant::toAnsiString(songTags.artist));
    if (!songTags.album.empty()) playList->getCol(posSongSelected, 2)->setValor(songTags.album);
    if (!songTags.album.empty()) playList->getCol(posSongSelected, 2)->setTexto(Constant::toAnsiString(songTags.album));

    //If we don't have correct time we don't set it
    if (!songTags.duration.empty() && songTags.duration.compare("0") != 0){
        double tagDuration = floor(Constant::strToTipo<double>(songTags.duration));
        string formatDuration = Constant::timeFormat(tagDuration);
        Traza::print("Actualizando tiempo a " + formatDuration, W_DEBUG);

        playList->getCol(posSongSelected, 3)->setValor(songTags.duration);
        playList->getCol(posSongSelected, 3)->setTexto(formatDuration);
        UIProgressBar *objProg = (UIProgressBar *)ObjectsMenu->getObjByName("progressBarMedia");
        objProg->setProgressMax(tagDuration);
        ObjectsMenu->getObjByName("mediaTimerTotal")->setLabel(formatDuration);

    }
    Traza::print("Redibujar playlist", W_DEBUG);
    playList->setImgDrawed(false);
    return 0;
}

/**
*
*/
void Jukebox::downloadFile(string ruta){
    string tempFileDir = Constant::getAppDir() + FILE_SEPARATOR + "temp.ogg";
    setFileDownloaded(false);
    Traza::print("Jukebox::downloadFile. Descargando fichero " + ruta, W_DEBUG);

    //Creamos el servidor de descarga y damos valor a sus propiedades
    if (this->getServerSelected() == GOOGLEDRIVESERVER){
        this->serverDownloader = new GoogleDrive(this->getServerCloud(this->getServerSelected()));
    } else if (this->getServerSelected() == DROPBOXSERVER){
        this->serverDownloader = new Dropbox(this->getServerCloud(this->getServerSelected()));
    } else if (this->getServerSelected() == ONEDRIVESERVER){
        this->serverDownloader = new Onedrive(this->getServerCloud(this->getServerSelected()));
    }

    //Realizamos la descarga del fichero
    this->serverDownloader->getFile(tempFileDir, ruta, this->serverDownloader->getAccessToken());
    Traza::print("Jukebox::downloadFile. Fichero " + ruta + " descargado", W_DEBUG);
    //Actualizamos las propiedades por si ha habido un cambio en el token por un refreshtoken de oauth
    // (Por ahora solo en Google drive)
//    this->getServerCloud(this->getServerSelected())->setProperties(this->serverDownloader);

    if (serverDownloader != NULL){
        //Liberamos los recursos
        delete this->serverDownloader;
        this->serverDownloader = NULL;
    }
    setFileDownloaded(true);
}

/**
*
*/
void Jukebox::abortDownload(){
    if (this->serverDownloader != NULL){
        this->serverDownloader->abortDownload();
        delete this->serverDownloader;
        this->serverDownloader = NULL;
    }
}

/**
*
*/
void Jukebox::abortServers(){
    for (int i=0; i < MAXSERVERS; i++){
        arrCloud[i]->abortDownload();
    }
    aborted = true;
}

/**
*
*/
void Jukebox::addLocalAlbum(string ruta){
    Traza::print("Jukebox::addLocalAlbum", W_DEBUG);
    UITreeListBox *albumList = ((UITreeListBox *)ObjectsMenu->getObjByName("albumList"));
    Dirutil dir;
    string nombreAlbum = dir.getFolder(ruta);
    nombreAlbum = nombreAlbum.substr(nombreAlbum.find_last_of(FILE_SEPARATOR)+1);

    if (dir.existe(ruta)){
        Traza::print("Jukebox::addLocalAlbum anyadiendo: " + ruta, W_DEBUG);
        albumList->add("0", nombreAlbum, dir.getFolder(ruta), music, -1, true);
    }
    Traza::print("Jukebox::addLocalAlbum END", W_DEBUG);
}

/**
*
*/
uint32_t Jukebox::refreshPlayListMetadataFromId3Dir(){
    UIListGroup *playList = ((UIListGroup *)ObjectsMenu->getObjByName("playLists"));
    double duration = 0.0;
    canPlay = false;

    Dirutil dir;
//    listaSimple<FileProps> *filelist = new listaSimple<FileProps>();
    vector<FileProps> *filelist = new vector<FileProps>();
    FileProps file;
    string ruta = rutaInfoId3;
    ruta = dir.getFolder(ruta);

    playList->clearLista();
    //dir.listarDir(ruta.c_str(), filelist, filtroFicheros);
    dir.listarDirRecursivo(ruta, filelist);
    int posFound = 0;
    int pos = 0;


    for (unsigned int i=0; i < filelist->size(); i++){
            file = filelist->at(i);
            if (filtroFicherosReproducibles.find(dir.getExtension(file.filename)) != string::npos ){
                vector <ListGroupCol *> miFila;
                miFila.push_back(new ListGroupCol(file.filename, file.dir + FILE_SEPARATOR + file.filename));
                miFila.push_back(new ListGroupCol("",""));
                miFila.push_back(new ListGroupCol("",""));
                miFila.push_back(new ListGroupCol(Constant::timeFormat(0), "0"));
                playList->addElemLista(miFila);
                if (rutaInfoId3.compare(file.dir + FILE_SEPARATOR + file.filename) == 0){
                    posFound = pos;
                }
                pos++;
            }
    }
    playList->calcularScrPos();
    playList->setPosActualLista(posFound);
    playList->refreshLastSelectedPos();
    playList->calcularScrPos();

    delete filelist;
    playList->setImgDrawed(false);
    canPlay = true;
    Transcode trans;

    for (unsigned int i=0; i < playList->getSize(); i++){
        string file = playList->getValue(i);
        Traza::print("Obteniendo datos de la cancion: " + file, W_DEBUG);
        TID3Tags songTags = trans.getSongInfo(file);
        Traza::print("Datos obtenidos", W_DEBUG);
        if (rutaInfoId3.compare(playList->getValue(i)) == 0){
            duration = floor(Constant::strToTipo<double>(songTags.duration));
        }
        if (!songTags.title.empty()) playList->getCol(i, 0)->setTexto(songTags.title);
        if (!songTags.artist.empty()) playList->getCol(i, 1)->setValor(songTags.artist);
        if (!songTags.artist.empty()) playList->getCol(i, 1)->setTexto(songTags.artist);
        if (!songTags.album.empty()) playList->getCol(i, 2)->setValor(songTags.album);
        if (!songTags.album.empty()) playList->getCol(i, 2)->setTexto(songTags.album);
        if (!songTags.duration.empty()) playList->getCol(i, 3)->setValor(songTags.duration);
        if (!songTags.duration.empty()) playList->getCol(i, 3)
                            ->setTexto(Constant::timeFormat(floor(Constant::strToTipo<double>(songTags.duration))));
        playList->setImgDrawed(false);
    }

    Traza::print("Playlist actualizado", W_DEBUG);
    UIProgressBar *objProg = (UIProgressBar *)ObjectsMenu->getObjByName("progressBarMedia");
    int max_ = objProg->getProgressMax();
    Traza::print("Actualizando barra de progreso con el valor", max_, W_DEBUG);
    //Actualizamos la barra de progreso en el caso de que no tuvieramos informacion del maximo de duracion
    if (max_ == 0){
        max_ = duration;
        objProg->setProgressMax(max_);
        ObjectsMenu->getObjByName("mediaTimerTotal")->setLabel(Constant::timeFormat(max_));
    }
    Traza::print("Redibujar playlist", W_DEBUG);

    return 0;
}

/**
*
*/
uint32_t Jukebox::authenticateServers(){
    uint32_t salida = SINERROR;
    setServersAuthenticated(false);
    for (int i=0; i < MAXSERVERS && !aborted; i++){
        try{
            int error = arrCloud[i]->authenticate();
            if (error == ERRORREFRESHTOKEN){
                arrCloud[i]->storeAccessToken("", true);
            } else if (error != SINERROR){
                salida = error;
            }
        } catch (Excepcion &e){
            Traza::print("Excepcion capturada", W_DEBUG);
        }
    }
    setServersAuthenticated(true);
    return salida;
}

/**
*
*/
uint32_t Jukebox::refreshAlbum(){
    Traza::print("Jukebox::refreshAlbum", W_DEBUG);
    UITreeListBox *albumList = ((UITreeListBox *)ObjectsMenu->getObjByName("albumList"));
    albumList->clearLista();
    CloudFiles files;
    string ruta;
    string idRuta;
    int albumId = 0;

    for (int serverID=0; serverID < MAXSERVERS && !aborted; serverID++){
        IOauth2 *server = this->getServerCloud(serverID);

        if (serverID == GOOGLEDRIVESERVER){
            idRuta = ((GoogleDrive *)server)->fileExist("Music","", server->getAccessToken());
            server->listFiles(idRuta, server->getAccessToken(), &files);
        } else {
            server->listFiles("/" + musicDir, server->getAccessToken(), &files);
        }

        string discName;
        for (unsigned int i=0; i < files.fileList.size(); i++){
            if (files.fileList.at(i)->isDir){
                ruta = files.fileList.at(i)->path;
                discName = ruta.substr(ruta.find_last_of("/")+1);
                discName = discName;
                albumList->add(Constant::TipoToStr(albumId), discName, files.fileList.at(i)->strHash, music, serverID, true);
                Traza::print("Jukebox::refreshAlbum anyadido: " + ruta, W_DEBUG);
                albumId++;
            }
        }
        files.clear();
    }
    albumList->sort();
    albumList->calcularScrPos();

    Traza::print("Jukebox::refreshAlbum END", W_DEBUG);
    return 0;
}

/**
*
*/
void Jukebox::uploadMusicToServer(string ruta){
    string rutaMetadata = ruta + FILE_SEPARATOR + fileMetadata;
    Dirutil dir;
    vector<FileProps> *filelist = new vector<FileProps>();
    string filterAndtxt = filterUploadExt + " .txt"; //We add txt to upload metadata

    dir.listarDirRecursivo(ruta, filelist, filterAndtxt);
    FileProps file;
    int countFile = 0;
    string rutaLocal;
    string nombreAlbum;
    string lastNombreAlbum;
    string rutaUpload;
    IOauth2 *server = this->getServerCloud(this->getServerSelected());

    if (!server->getAccessToken().empty() && filelist->size() > 0){

        for (unsigned int i=0; i < filelist->size(); i++){
            file = filelist->at(i);
            rutaLocal = file.dir + FILE_SEPARATOR + file.filename;

            Traza::print("ruta: " + ruta, W_DEBUG);

            nombreAlbum = generarNombreAlbum(&file, ruta);

            if (this->getServerSelected() == GOOGLEDRIVESERVER){
                rutaUpload = generarDirGoogleDrive(nombreAlbum);
            } else {
                rutaUpload = "/" + musicDir + "/" + nombreAlbum + "/" + file.filename;
            }

            Traza::print("musicDir: " + musicDir +
                    "; nombreAlbum: " + nombreAlbum +
                    "; filename: " + file.filename, W_DEBUG);

            string fileExt = dir.getExtension(file.filename);
            Constant::lowerCase(&fileExt);

            if ( (filterUploadExt.find(fileExt) != string::npos && file.filename.compare("..") != 0)
                    || file.filename.compare(fileMetadata) == 0){
                countFile++;
                int percent = countFile/(float)filelist->size()*100;
                ObjectsMenu->getObjByName("statusMessage")->setLabel("Subiendo "
                            + Constant::TipoToStr(percent) + "% "
                            + " " + file.filename);

                Traza::print("Subiendo fichero...", W_DEBUG);
                Traza::print("Confirmando subida del album " + rutaUpload + "...", W_DEBUG);
                bool ret = server->chunckedUpload(rutaLocal, rutaUpload, server->getAccessToken());
                //Si habiamos convertido el fichero, lo borramos
                if (convertedFilesList->find(rutaLocal) != -1 && ret){
                    dir.borrarArchivo(rutaLocal);
                }
            }
            lastNombreAlbum = nombreAlbum;
        }

        if (subirMetadatos(nombreAlbum, rutaUpload, rutaMetadata))
            dir.borrarArchivo(rutaMetadata);

        refreshAlbum();
        UITreeListBox *albumList = ((UITreeListBox *)ObjectsMenu->getObjByName("albumList"));
        albumList->setImgDrawed(false);
        ObjectsMenu->getObjByName("statusMessage")->setLabel("Album " + nombreAlbum + " subido");
    }

}
/**
*
*/
string Jukebox::generarNombreAlbum(FileProps *file, string ruta){
    string nombreAlbum;

    string parentDir = ruta.substr(0, ruta.find_last_of(FILE_SEPARATOR));

    nombreAlbum = file->dir.substr(parentDir.length()+1);
    //Comprobamos si la ruta indicada tiene subdirectorios
    if (nombreAlbum.empty()){
        nombreAlbum = file->dir.substr(file->dir.find_last_of(FILE_SEPARATOR) + 1);
    }

    nombreAlbum = Constant::replaceAll(nombreAlbum, tempFileSep, "/");

//    else {
//        //Hay subdirectorios, obtenemos el nombre del directorio de la ruta actual
//        //y le concatenamos el subdirectorio entero
//        nombreAlbum = ruta.substr(ruta.find_last_of(FILE_SEPARATOR) + 1);
//        string subdir = file->dir.substr(file->dir.find_last_of(FILE_SEPARATOR) + 1);
//        subdir = Constant::replaceAll(subdir, tempFileSep, "_");
//        nombreAlbum += " " + subdir;
//    }
    return nombreAlbum;
}

/**
*
*/
string Jukebox::generarDirGoogleDrive(string nombreAlbum){
    IOauth2 *server = this->getServerCloud(this->getServerSelected());
    string idMusic = ((GoogleDrive *)server)->fileExist("Music","", server->getAccessToken());
    if (idMusic.empty()){
        string idDir = ((GoogleDrive *)server)->mkdir(DIRCLOUD, "", server->getAccessToken());
        idMusic = ((GoogleDrive *)server)->mkdir("Music", idDir, server->getAccessToken());
    }
    string idRutaUpload = ((GoogleDrive *)server)->fileExist(nombreAlbum,idMusic, server->getAccessToken());
    if (idRutaUpload.empty()){
        idRutaUpload = ((GoogleDrive *)server)->mkdir(nombreAlbum, idMusic, server->getAccessToken());
    }
    return idRutaUpload;
}

/**
*
*/
bool Jukebox::subirMetadatos(string nombreAlbum, string rutaUpload, string rutaMetadata){
    IOauth2 *server = this->getServerCloud(this->getServerSelected());
    ObjectsMenu->getObjByName("statusMessage")->setLabel("Subiendo " + fileMetadata);
    Traza::print("Subiendo metadatos...", W_DEBUG);
    if (this->getServerSelected() != GOOGLEDRIVESERVER){
        rutaUpload = "/" + musicDir + "/" + nombreAlbum + "/" + fileMetadata;
    }
    Traza::print("Confirmando metadatos " + rutaUpload + "...", W_DEBUG);
    return server->chunckedUpload(rutaMetadata, rutaUpload, server->getAccessToken());
}

/**
*
*/
int Jukebox::extraerCD(string cdDrive, string extractionPath, CdTrackInfo *cdTrack){
    Dirutil dir;
    string msg;
    ULONG i = 0;
    int padSize = 0;
    CAudioCD audioCD;

    //Abrimos el CD de audio
    if ( ! audioCD.Open( cdDrive.at(0) ) ){
        Traza::print("Jukebox::extraerCD No se puede abrir la unidad " + cdDrive, W_DEBUG);
        ObjectsMenu->getObjByName("statusMessage")->setLabel("No se puede abrir la unidad " + cdDrive);
        return 0;
    }

    //Obtenemos el numero de pistas
    ULONG TrackCount = audioCD.GetTrackCount();
    Traza::print("Jukebox::extraerCD Disc ID: " + audioCD.getCddbID() + ", Numero de pistas: " + Constant::TipoToStr(TrackCount), W_DEBUG);
    ObjectsMenu->getObjByName("statusMessage")->setLabel("Extrayendo " + Constant::TipoToStr(TrackCount) + " pistas");
    //Obtenemos informacion del cd
    this->getCddb(&audioCD, cdTrack);

    if (!extractionPath.empty()){
        string albumName = Constant::removeEspecialChars(
                                Constant::replaceAll(
                                Constant::toAnsiString(cdTrack->albumName), "/", "-"));

        padSize = Constant::TipoToStr(TrackCount).length();

        if (albumName.empty()){
            albumName = "rip_" + Constant::replaceAll(Constant::fecha(), ":", "_");
        }

        string rutaRip = extractionPath + FILE_SEPARATOR + "rip"
                        + FILE_SEPARATOR
                        + albumName;

        Traza::print("Iofrontend::extraerCD. Extrayendo CD a la ruta: "+ rutaRip, W_DEBUG);
        if (!existe(extractionPath)){
            ObjectsMenu->getObjByName("statusMessage")->setLabel("No se puede acceder al directorio " + extractionPath);
            return 0;
        }
        dir.mkpath(string(extractionPath + FILE_SEPARATOR + "rip").c_str(), 0777);
        dir.mkpath(rutaRip.c_str(), 0777);

        for (i=0; i < TrackCount; i++ ){
            ULONG Time = audioCD.GetTrackTime( i );

            msg = Constant::toAnsiString("Track " + Constant::TipoToStr(i+1) + " Tiempo: " + Constant::TipoToStr(Time/60) + ":"
                 + Constant::TipoToStr(Time%60) + " TamaÃ±o: "
                 + Constant::TipoToStr(ceil (audioCD.GetTrackSize(i) / double( pow(1024, 2)))) + " MB");

            ObjectsMenu->getObjByName("statusMessage")->setLabel(msg);

            string songName;

            if (cdTrack->titleList.size() > 0 && i < cdTrack->titleList.size()){
                songName = rutaRip + FILE_SEPARATOR + Constant::pad(Constant::TipoToStr(i+1), padSize, '0')
                           + " - " + cdTrack->titleList.at(i) + ".wav";
            } else {
                songName = rutaRip + FILE_SEPARATOR +  "Track "
                        + Constant::pad(Constant::TipoToStr(i+1), padSize, '0') + ".wav";
            }

            // Save track-data to file...
            int ret = audioCD.ExtractTrack( i, songName.c_str());
            if ( ret != 0 ){
                ObjectsMenu->getObjByName("statusMessage")->setLabel("No se puede extraer la pista " + Constant::TipoToStr(i));
                Traza::print("No se puede extraer la pista " + Constant::TipoToStr(i) + " con error: " + Constant::TipoToStr(ret), W_DEBUG);
            }
            // ... or just get the data into memory
    //        CBuf<char> Buf;
    //        if ( ! AudioCD.ReadTrack(i, &Buf) )
    //            printf( "Cannot read track!\n" );
        }

    }

    audioCD.Close();
    return i;
}

/**
* Obtiene la informacion exacta de cada cancion del disco. Es necesario que
* ya se sepa el id del disco y su categoria. Ver la llamada a Jukebox::searchCddbAlbums
*/
int Jukebox::getCddb(CAudioCD *audioCD, CdTrackInfo *cdTrack){
    Freedb cddb;
    FreedbQuery query;
    vector<CdTrackInfo *> cdTrackList;
    query.discId = this->getIdSelected();
    query.categ = this->getCategSelected();
    loadConfigCDDB(&query);
    int code = cddb.getCdInfo(&query, cdTrack);
    return code;
}

/**
*
*/
uint32_t Jukebox::searchCddbAlbums(){
    cddbObtained = false;
    if (this->cdTrackList != NULL && !this->cdDrive.empty()){
        this->cdTrackList->clear();
        searchCddbAlbums(this->cdDrive, this->cdTrackList);
    }
    cddbObtained = true;
    return 0;
}

/**
*
*/
int Jukebox::searchCddbAlbums(string cdDrive, vector<CdTrackInfo *> *cdTrackList){

    CAudioCD audioCD;
    //Abrimos el CD de audio
    if ( ! audioCD.Open( cdDrive.at(0) ) ){
        return 0;
    }
    CdTrackInfo cddbTrack;

    Freedb cddb;
    FreedbQuery query;
    query.discId = audioCD.getCddbID();
    loadConfigCDDB(&query);
    query.totalSeconds = audioCD.getDiscSeconds();

    std::vector<CDTRACK> *cdInfo = audioCD.getCdInfo();
    for (size_t i=0; i < cdInfo->size(); i++){
        query.offsets.push_back(cdInfo->at(i).Offset);
    }

    //Listamos todos los posibles albums
    int code = cddb.searchCd(&query, cdTrackList);
    Traza::print("Codigo", code, W_DEBUG);
    audioCD.Close();
    return cdTrackList->size();
}


/**
* Comprueba si existe el directorio o fichero pasado por parÃ¯Â¿Â½metro
*/
bool Jukebox::existe(string ruta){
    Traza::print("Jukebox::existe " + ruta, W_DEBUG);
    if(isDir(ruta)){
         Traza::print("El directorio existe", W_DEBUG);
        return true;
    } else {
        FILE *archivo = fopen(ruta.c_str(), "r");
        if (archivo != NULL) {
            fclose(archivo);
            Traza::print("El fichero existe", W_DEBUG);
            return true; //TRUE
        } else {
            Traza::print("El fichero no existe", W_DEBUG);
            return false; //FALSE
        }
    }
}

/**
*
*/
bool Jukebox::isDir(string ruta){
    struct stat info;
    stat(ruta.c_str(), &info);

    if(S_ISDIR(info.st_mode)){
        Traza::print("Fichero es un directorio", W_DEBUG);
        return true;
    } else {
        Traza::print("Fichero no es un directorio", W_DEBUG);
        return false;
    }

}

void Jukebox::loadConfigCDDB(FreedbQuery *query){
    string configIniFile = Constant::getAppDir() + Constant::getFileSep() + "config.ini";
    ListaIni<Data> *config = new ListaIni<Data>();
    config->loadFromFile(configIniFile);
    config->sort();

    query->username =     config->find("cddbuser") >= 0 ? config->get(config->find("cddbuser")).getValue() : "";
    query->hostname = config->find("cddbhostname") >= 0 ? config->get(config->find("cddbhostname")).getValue() : "";
    query->clientname =     config->find("cddbclientname") >= 0 ? config->get(config->find("cddbclientname")).getValue() : "";
    query->version =  config->find("cddbclientversion") >= 0 ? config->get(config->find("cddbclientversion")).getValue() : "";
    delete config;
}

