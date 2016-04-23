#ifndef JUKEBOX_H
#define JUKEBOX_H

#include "Launcher.h"
#include "Menuobject.h"
#include "servers/dropbox.h"
#include "servers/googledrive.h"
#include "listaIni.h"
#include "thread.h"

using namespace std;

static const string filtroFicheros = ".mp3,.mid,.wav,.wma,.cda,.aac,.ac3,.flac,.mp4,.ogg";
static const string filtroFicherosReproducibles = ".mp3,.mid,.wav,.ogg,.flac";

struct TID3Tags{
    string album;
    string title;
    string duration;
    string track;
    string genre;
    string publisher;
    string composer;
    string artist;
    string date;
};

const char arrTags[9][20] = {{"album"},{"title"},
{"duration"},{"track"},{"genre"},{"publisher"},
{"composer"},{"artist"},{"date"}};

typedef enum{tagAlbum, tagTitle, tagDuration,tagTrack,tagGenre,tagPublisher,tagComposer,tagArtist,tagDate,tagMAX} id3Pos;
typedef enum{DROPBOXSERVER, GOOGLEDRIVESERVER, MAXSERVERS} cloudServers;
const char arrNameServers[3][20] = {{"Drobox"},{"Google"},{"undefined"}};

class Jukebox
{
    public:
        Jukebox();
        virtual ~Jukebox();

        TID3Tags getSongInfo(string filepath);
        void convertir(string ruta);
        DWORD convertir();
        DWORD uploadMusicToDropbox();
        DWORD refreshAlbumAndPlaylist();
        DWORD refreshPlaylist();
        DWORD downloadFile();
        DWORD refreshAlbum();
        DWORD refreshPlayListMetadata();
        DWORD refreshPlayListMetadataFromId3Dir();
        DWORD authenticateServers();


        void setObjectsMenu(tmenu_gestor_objects *var){ObjectsMenu = var;}
        void setDirToUpload(string var){dirToUpload = var;}
        void setFileToDownload(string var){fileToDownload = var;}
        void uploadMusicToDropbox(string ruta);
//        void refreshPlaylist(string rutaAlbumDropbox);
        void downloadFile(string ruta);
        void abortDownload();
        void addLocalAlbum(string ruta);
        void setRutaInfoId3(string var){rutaInfoId3 = var;}
        bool isCanPlay(){return canPlay;}
        void setCanPlay(bool var){canPlay = var;}
        void setConcatNameFolder(bool var){concatNameFolder = var;}

        void setServerSelected(int var){serverSelected = var;}
        int getServerSelected(){return serverSelected;}

        void setAlbumSelected(string var){albumSelected = var;}
        string getAlbumSelected(){return albumSelected;}

        IOauth2 *getServerCloud(int idServer){
            if (idServer >= 0 && idServer < MAXSERVERS)
                return arrCloud[idServer];
            else
                return NULL;
        }

    protected:

    private:
        tmenu_gestor_objects *ObjectsMenu;
        string dirToUpload;
        string fileToDownload;
        int serverSelected;
        string albumSelected;
        bool concatNameFolder;

        void hashMapMetadatos(map<string, string> *metadatos, string ruta);
//        Dropbox dropboxDownloader;
        IOauth2 *serverDownloader;

        string getMetadatos(map<string, string> *metadatos, string key);
        listaSimple<string> *convertedFilesList;
        string rutaInfoId3;
        static bool canPlay;
        IOauth2 * arrCloud[MAXSERVERS];
        string generarNombreAlbum(FileProps *file, string ruta);
        string generarDirGoogleDrive(string nombreAlbum);
        void subirMetadatos(string nombreAlbum, string rutaUpload, string rutaMetadata);

};

#endif // JUKEBOX_H
