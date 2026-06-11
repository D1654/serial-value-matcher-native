#include <QApplication>

#include "app/main_window.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    svm::app::MainWindow window;
    window.show();
    return app.exec();
}
