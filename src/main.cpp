#include <QtEndian>
#include <QtWidgets>
#include <QSaveFile>
#include <QLockFile>
#include "canvas.h"
#include "loader.h"
#include "thumbnail.h"

class Dialog : public QDialog {
    Canvas *canvas, *target; bool automatic=false; QComboBox *mode, *view, *resolution; QSpinBox *mb, *triangles;
    QPushButton *render, *cancel; QPlainTextEdit *log; QProgressBar *progress;
    QString folder, cache, manifestPath; QStringList files; QJsonObject manifest;
    Loader *loader=nullptr; int index=0; bool stopped=false, running=false; int failures=0;
    QLockFile lock; QSettings settings;
    QString uri(const QString &p) { return QUrl::fromLocalFile(p).toString(QUrl::FullyEncoded); }
    QString key(const QString &p) { return QCryptographicHash::hash(uri(p).toUtf8(), QCryptographicHash::Md5).toHex(); }
    QList<QPair<QString,int>> thumbnailBuckets() const {
        return {{"normal",128},{"large",256},{"x-large",512},{"xx-large",1024}};
    }
    void appearance() {
        for(Canvas *c:{canvas,target}) {
            c->set_drawMode(static_cast<DrawMode>(mode->currentIndex()));
            c->common_view_change(static_cast<ViewPoint>(view->currentData().toInt()));
            c->common_view_change(centerview); c->update();
        }
    }
    void persist() {
        for (auto pair : {qMakePair(QString("mode"),mode->currentIndex()),qMakePair(QString("view"),view->currentIndex()),qMakePair(QString("resolution"),resolution->currentIndex()),qMakePair(QString("mb"),mb->value()),qMakePair(QString("triangles"),triangles->value())}) settings.setValue(pair.first,pair.second);
    }
    bool saveManifest() {
        QSaveFile f(manifestPath);
        if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(manifest).toJson())<0 || !f.commit()) { log->appendPlainText("Could not save cache index."); return false; }
        return true;
    }
    void finish() {
        if(!saveManifest()) failures++; lock.unlock(); running=false; render->setEnabled(true); cancel->setEnabled(false);
        mode->setEnabled(true); view->setEnabled(true); resolution->setEnabled(true); mb->setEnabled(true); triangles->setEnabled(true);
        target->clearMesh(); target->hide();
        log->appendPlainText(stopped ? "Stopped. Completed previews are saved." : "Done. Thumbnails saved.");
        if(automatic) QCoreApplication::exit(failures ? 1 : 0);
        else if(!stopped && !failures) accept();
    }
    void next() {
        if (stopped || index>=files.size()) { finish(); return; }
        QString path=files[index++]; progress->setValue(index-1);
        QFileInfo info(path);
        if(info.size()>qint64(mb->value())*1024*1024) { log->appendPlainText("Skipped (size): "+info.fileName()); QTimer::singleShot(0,this,[this]{next();}); return; }
        // Reject oversized binary meshes before allocating parsing buffers.
        QFile input(path); if(input.open(QIODevice::ReadOnly) && input.size()>=84) {
            QByteArray header=input.read(84); quint32 n=qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(header.constData()+80));
            if(input.size()==84+qint64(n)*50 && triangles->value()>0 && n>quint32(triangles->value())) {
                log->appendPlainText("Skipped (triangles): "+info.fileName()); QTimer::singleShot(0,this,[this]{next();}); return;
            }
        }
        loader=new Loader(this,path,false); auto *active=loader;
        const qint64 sourceSize=info.size(), sourceTime=info.lastModified().toMSecsSinceEpoch();
        connect(active,&Loader::got_mesh,this,[this,path,sourceSize,sourceTime](Mesh *mesh,bool){
            if(stopped) {delete mesh; return;}
            if(triangles->value()>0 && mesh->triCount()>triangles->value()) {delete mesh; log->appendPlainText("Skipped (triangles): "+QFileInfo(path).fileName());return;}
            QFileInfo check(path);
            if(check.size()!=sourceSize || check.lastModified().toMSecsSinceEpoch()!=sourceTime) {delete mesh;log->appendPlainText("Changed while loading; skipped: "+path);return;}
            target->load_mesh(mesh,false); appearance();
            QImage image=target->grabFramebuffer();
            if(image.isNull()) {failures++;log->appendPlainText("Render failed: "+path);return;}
            QFileInfo current(path);
            if(!current.exists()) return;
            const QMap<QString,QString> metadata = {
                {"Thumb::URI",uri(path)},
                {"Thumb::MTime",QString::number(current.lastModified().toSecsSinceEpoch())},
                {"Thumb::Size",QString::number(current.size())},
                {"Software","stl-folder-preview"}
            };
            bool saved=true;
            for(const auto &bucket:thumbnailBuckets()) {
                const int size=qMin(bucket.second,image.width());
                const QByteArray png=thumbnailPng(image.scaled(size,size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation),metadata);
                QSaveFile output(cache+"/"+bucket.first+"/"+key(path)+".png");
                if(png.isEmpty() || !output.open(QIODevice::WriteOnly) || output.write(png)!=png.size() || !output.commit()) saved=false;
            }
            if(saved) {
                manifest.insert(path,key(path)); log->appendPlainText("Rendered: "+current.fileName());
            } else {failures++; manifest.insert(path,key(path));log->appendPlainText("Could not save every thumbnail size: "+path);}

        });
        auto fail=[this,path]{log->appendPlainText("Invalid or unreadable STL: "+QFileInfo(path).fileName());};
        connect(active,&Loader::error_bad_stl,this,fail); connect(active,&Loader::error_empty_mesh,this,fail); connect(active,&Loader::error_missing_file,this,fail);
        connect(active,&QThread::finished,this,[this,active]{loader=nullptr;active->deleteLater();progress->setValue(index);QTimer::singleShot(0,this,[this]{next();});}); active->start();
    }
    void start() {
        if(!lock.tryLock(0)) {QMessageBox::information(this,"Render in progress","Another preview render is running. Try again after it finishes."); return;}
        if(!canvas->isValid()) {lock.unlock();QMessageBox::warning(this,"OpenGL unavailable","The renderer needs a working desktop OpenGL context.");return;}
        target->setFixedSize(resolution->currentData().toInt(),resolution->currentData().toInt());target->show();
        persist(); QFile f(manifestPath); manifest=QJsonObject();
        if(f.open(QIODevice::ReadOnly)) manifest=QJsonDocument::fromJson(f.readAll()).object();
        int removed=0;
        for(const QString &p:manifest.keys()) if(!QFileInfo::exists(p)) {
            // Only delete our own thumbnail, never an image another app has replaced.
            for(const auto &bucket:thumbnailBuckets()) {
                const QString cached=cache+"/"+bucket.first+"/"+key(p)+".png";
                QImage reader(cached);
                if(reader.text("Software")=="stl-folder-preview" && reader.text("Thumb::URI")==uri(p)) {if(QFile::remove(cached)) removed++;}
            }
            manifest.remove(p);
        }
        log->clear(); log->appendPlainText(QString("Removed %1 previews for missing files.").arg(removed));
        files.clear(); for(const QFileInfo &entry:QDir(folder).entryInfoList(QDir::Files,QDir::Name)) if(entry.suffix().compare("stl",Qt::CaseInsensitive)==0) files<<entry.absoluteFilePath();
        index=0;stopped=false;running=true;failures=0; progress->setRange(0,files.size());progress->setValue(0);
        render->setEnabled(false);cancel->setEnabled(true);mode->setEnabled(false);view->setEnabled(false);resolution->setEnabled(false);mb->setEnabled(false);triangles->setEnabled(false);next();
    }
    void sample() {
        for(const QFileInfo &f:QDir(folder).entryInfoList(QDir::Files,QDir::Name)) if(f.suffix().compare("stl",Qt::CaseInsensitive)==0 && f.size()<=30*1024*1024) {
            loader=new Loader(this,f.absoluteFilePath(),false);auto *active=loader;render->setEnabled(false);
            connect(active,&Loader::got_mesh,this,[this](Mesh *mesh,bool){if(mesh->triCount()>2000000){delete mesh;return;}canvas->load_mesh(mesh,false);appearance();});
            connect(active,&QThread::finished,this,[this,active]{loader=nullptr;active->deleteLater();render->setEnabled(true);}); active->start();return;
        }
        log->appendPlainText("No STL under 30 MB available for the sample. Render still works with your chosen limits.");
    }
public:
    Dialog(QString path,bool autoRender=false):folder(QFileInfo(path).absoluteFilePath()),cache(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)+"/thumbnails"),manifestPath(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/index.json"),lock(manifestPath+".lock") {
        QDir().mkpath(QFileInfo(manifestPath).absolutePath());for(const auto &bucket:thumbnailBuckets()) QDir().mkpath(cache+"/"+bucket.first);
        automatic=autoRender;
        setWindowTitle("Render STL Preview");resize(560,760);auto *layout=new QVBoxLayout(this);
        auto *label=new QLabel(folder);label->setWordWrap(true);layout->addWidget(label);
        QSurfaceFormat format;format.setDepthBufferSize(24);format.setSamples(4);
        canvas=new Canvas(format);canvas->setFixedSize(320,320);canvas->draw_axes(false);canvas->clear_status();layout->addWidget(canvas,0,Qt::AlignHCenter);
        target=new Canvas(format,this);target->setWindowFlags(Qt::Tool);target->setAttribute(Qt::WA_DontShowOnScreen);target->draw_axes(false);target->clear_status();
        auto *form=new QFormLayout;layout->addLayout(form);
        mode=new QComboBox;mode->addItems({"Shaded polygons","Wireframe","Surface angle colors","Custom lighting","Solid + wireframe"});mode->setCurrentIndex(settings.value("mode",2).toInt());form->addRow("Style",mode);
        view=new QComboBox;for(auto pair:{qMakePair(QString("Isometric"),isoview),qMakePair(QString("Top"),topview),qMakePair(QString("Front"),frontview),qMakePair(QString("Right"),rightview)}) view->addItem(pair.first,int(pair.second));view->setCurrentIndex(settings.value("view",0).toInt());form->addRow("Camera",view);
        resolution=new QComboBox;for(int n:{256,512,1024})resolution->addItem(QString::number(n)+" px",n);resolution->setCurrentIndex(settings.value("resolution",0).toInt());form->addRow("Image size",resolution);
        mb=new QSpinBox;mb->setRange(1,4096);mb->setValue(settings.value("mb",30).toInt());mb->setSuffix(" MiB");form->addRow("Skip files larger than",mb);
        triangles=new QSpinBox;triangles->setRange(0,20000000);triangles->setSingleStep(100000);triangles->setSpecialValueText("Unlimited");triangles->setValue(settings.value("triangles",2000000).toInt());form->addRow("Maximum triangles",triangles);
        layout->addWidget(new QLabel("Current folder only • Cleanup runs when you press Render"));
        progress=new QProgressBar;layout->addWidget(progress);log=new QPlainTextEdit;log->setReadOnly(true);layout->addWidget(log);
        auto *buttons=new QHBoxLayout;layout->addLayout(buttons);render=new QPushButton("Render");cancel=new QPushButton("Stop");cancel->setEnabled(false);buttons->addWidget(render);buttons->addWidget(cancel);
        connect(render,&QPushButton::clicked,this,[this]{start();});connect(cancel,&QPushButton::clicked,this,[this]{stopped=true;});
        connect(mode,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{appearance();});connect(view,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{appearance();});
        QSettings opener("JoeMirai","stl-browser");for(Canvas *c:{canvas,target}) { c->setAppearance(opener.value("modelColor",QColor("white")).value<QColor>(),opener.value("backgroundColor",QColor("#173b46")).value<QColor>(),opener.value("classicBackground",true).toBool());
        c->view_perspective(opener.value("projection","perspective").toString()=="perspective"?Canvas::P_PERSPECTIVE:Canvas::P_ORTHOGRAPHIC,false);
        c->setAmbientColor(opener.value("ambientColor",QColor("#38ccff")).value<QColor>());
        c->setDirectiveColor(opener.value("directiveColor",QColor("white")).value<QColor>());
        c->setAmbientFactor(opener.value("ambientFactor",0.67).toDouble());c->setDirectiveFactor(opener.value("directiveFactor",0.5).toDouble());
        c->setCurrentLightDirection(qBound(0,opener.value("currentLightDirection",1).toInt(),c->getNameDir().size()-1));
        }
        QTimer::singleShot(200,this,[this]{if(automatic)start();else sample();});
    }
    void reject() override {if(running || loader){stopped=true;log->appendPlainText("Please wait for the current file, then close.");return;}QDialog::reject();}
    void closeEvent(QCloseEvent *e) override {if(running||loader){stopped=true;e->ignore();}else e->accept();}
};
int main(int argc,char **argv) {QApplication app(argc,argv);QCoreApplication::setOrganizationName("JoeMirai");QCoreApplication::setApplicationName("stl-folder-preview");
    bool automatic=argc>1 && QString::fromLocal8Bit(argv[1])=="--render-and-exit";
    QString path=argc>(automatic?2:1)?QString::fromLocal8Bit(argv[automatic?2:1]):QDir::currentPath();if(!QFileInfo(path).isDir()){QMessageBox::warning(nullptr,"Choose a folder","Please select a local folder.");return 1;} Dialog dialog(path,automatic);dialog.show();return app.exec();}
