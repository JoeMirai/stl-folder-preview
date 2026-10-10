// Exercise the same dialog as the app, including a real Render button click.
#define main preview_application_main
#include "../src/main.cpp"
#undef main
#include <QTemporaryDir>

int main(int argc, char **argv) {
    QTemporaryDir temporary;
    qputenv("XDG_CACHE_HOME",(temporary.path()+"/cache").toUtf8());
    qputenv("XDG_CONFIG_HOME",(temporary.path()+"/config").toUtf8());
    QApplication app(argc,argv);
    QCoreApplication::setOrganizationName("JoeMirai");
    QCoreApplication::setApplicationName("stl-folder-preview");
    QFile model(temporary.path()+"/triangle.stl");
    if(!model.open(QIODevice::WriteOnly)) return 1;
    model.write("solid test\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\nendsolid test\n");model.close();
    Dialog dialog(temporary.path()); bool accepted=false,clicked=false;
    QObject::connect(&dialog,&QDialog::accepted,[&]{accepted=true;});
    QTimer click;click.setInterval(100);
    QObject::connect(&click,&QTimer::timeout,[&]{
        if(clicked) return;
        for(auto *button:dialog.findChildren<QPushButton *>()) if(button->text()=="Render" && button->isEnabled()) {
            clicked=true;click.stop();button->click();return;
        }
    });
    // The sample needs to start and finish before clicking Render.
    QTimer::singleShot(500,[&]{click.start();});
    QTimer::singleShot(20000,[&]{app.exit(1);});
    dialog.show();int code=app.exec();
    if(code || !clicked || !accepted || dialog.isVisible()) return 1;
    puts("PASS: clicking Render automatically closes the dialog after success");return 0;
}
