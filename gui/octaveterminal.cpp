#include "octaveterminal.h"
#include <QVBoxLayout>
#include <QPushButton>

OctaveTerminal::OctaveTerminal(QWidget *parent) :
    QMdiSubWindow(parent),
    m_client(0) {
    setWindowTitle("Octave Terminal");

    setWidget(new QWidget(this));
    m_mainToolBar = new QToolBar(widget());
    m_octaveOutput = new QTextBrowser(widget());
    m_commandLine = new QLineEdit(widget());

    QVBoxLayout *layout = new QVBoxLayout();
    layout->addWidget(m_mainToolBar);
    layout->addWidget(m_octaveOutput);
    layout->addWidget(m_commandLine);
    widget()->setLayout(layout);

    QPushButton *showEnvironmentButton = new QPushButton("Show Environment (who)");
    m_mainToolBar->addWidget(showEnvironmentButton);

    m_octaveOutput->setFontFamily("Monospace");
    m_octaveOutput->setReadOnly(true);

    blockUserInput();
    connect(m_commandLine, SIGNAL(returnPressed()), this, SLOT(sendCommand()));
    connect(showEnvironmentButton, SIGNAL(clicked()), this, SLOT(showEnvironment()));

    m_terminalHighlighter = new TerminalHighlighter(m_octaveOutput->document());
}

void OctaveTerminal::sendCommand() {
    QString command = m_commandLine->text();
    m_octaveOutput->setFontUnderline(true);
    m_octaveOutput->append(command);
    command.append("\n");
    m_client->send(command);
    m_commandLine->clear();
}

void OctaveTerminal::blockUserInput() {
    m_commandLine->setEnabled(false);
}

void OctaveTerminal::allowUserInput() {
    m_commandLine->setEnabled(true);
    m_commandLine->setFocus();
}

void OctaveTerminal::assignClient(Client *client) {
    m_client = client;
    connect(client, SIGNAL(dataAvailable()), this, SLOT(fetchDataFromClient()));
    connect(client, SIGNAL(errorAvailable()), this, SLOT(fetchErrorFromClient()));
    allowUserInput();
}

void OctaveTerminal::showEnvironment() {
    m_client->send("who\n");
}

void OctaveTerminal::fetchDataFromClient() {
    QString fetchedData = m_client->fetch();
    m_octaveOutput->setFontUnderline(false);
    m_octaveOutput->append(fetchedData);
}

void OctaveTerminal::fetchErrorFromClient() {
    QString error = m_client->errorMessage();
    m_octaveOutput->setFontUnderline(false);
    m_octaveOutput->append(error);
}
