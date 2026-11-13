// SPDX-License-Identifier: GPL-3.0-or-later
#include "orm_app_window.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>

#include <QCheckBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFont>
#include <QFontMetrics>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLayout>
#include <QList>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QTextEdit>
#include <QTextOption>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "common/logging.h"
#include "common/scm_rev.h"
#include "coop/protocol/messages.hpp"
#include "coop_notices.h"  // ORM "Guardado" on the HUD (room_saved)
#include "coop_window.h"

#ifndef ORM_APP_COOP_VERSION
#define ORM_APP_COOP_VERSION "unknown"
#endif

namespace OrmApp {

namespace {

constexpr int kStatusTimeoutMs = 20000;
constexpr int kShortTimeoutMs = 30000;
constexpr int kCheckTimeoutMs = 60000;
constexpr int kPlayTimeoutMs = 180000;  // play may fetch the room's save first
constexpr int kPollMs = 2000;           // files put in the folders by hand turn ✓ this soon

// The local paths of a drag's URLs ("" when it brings none).
QStringList DragPaths(const QMimeData* mime) {
    if (mime == nullptr || !mime->hasUrls()) {
        return {};
    }
    QStringList urls;
    for (const QUrl& url : mime->urls()) {
        urls << url.toString(QUrl::FullyEncoded);  // a '#' or '%' in a name survives
    }
    return DroppedPaths(urls);
}

QString EmulatorBuild() {
    return QStringLiteral("%1 + coop %2")
        .arg(QString::fromUtf8(Common::g_build_fullname), QStringLiteral(ORM_APP_COOP_VERSION));
}

QString OwnProtocol() {
    return QString::number(coop::protocol::kProtocolVersion);
}

void SetEnv(const QString& key, const QString& value) {
#ifdef _WIN32
    // The wide call, so a path with accents reaches the CRT and Win32
    // environments as it is. An empty value removes the variable.
    _wputenv_s(key.toStdWString().c_str(), value.toStdWString().c_str());
#else
    if (value.isEmpty()) {
        qunsetenv(key.toUtf8().constData());
    } else {
        qputenv(key.toUtf8().constData(), value.toUtf8());
    }
#endif
}

QPushButton* MakeButton(const QString& text, QWidget* parent) {
    auto* button = new QPushButton(text, parent);
    button->setAutoDefault(false);
    return button;
}

// A row of buttons that wraps onto the next line when the window is narrow
// (Qt's flow layout example, trimmed): every item keeps its size hint, so a
// button always shows its whole text.
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(int spacing) : spacing_(spacing) { setContentsMargins(0, 0, 0, 0); }
    ~FlowLayout() override {
        while (QLayoutItem* item = takeAt(0)) {
            delete item;
        }
    }
    void addItem(QLayoutItem* item) override { items_.append(item); }
    int count() const override { return static_cast<int>(items_.size()); }
    QLayoutItem* itemAt(int index) const override { return items_.value(index); }
    QLayoutItem* takeAt(int index) override {
        return index >= 0 && index < items_.size() ? items_.takeAt(index) : nullptr;
    }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return Arrange(QRect(0, 0, width, 0), false); }
    void setGeometry(const QRect& rect) override {
        QLayout::setGeometry(rect);
        Arrange(rect, true);
    }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override {
        QSize size;
        for (const QLayoutItem* item : items_) {
            size = size.expandedTo(item->minimumSize().expandedTo(item->sizeHint()));
        }
        return size;
    }

private:
    int Arrange(const QRect& rect, bool apply) const {
        int x = rect.x();
        int y = rect.y();
        int line = 0;
        for (QLayoutItem* item : items_) {
            if (item->isEmpty()) {
                continue;
            }
            const QSize hint = item->sizeHint();
            if (x > rect.x() && x + hint.width() > rect.right() + 1) {
                x = rect.x();
                y += line + spacing_;
                line = 0;
            }
            if (apply) {
                item->setGeometry(QRect(QPoint(x, y), hint));
            }
            x += hint.width() + spacing_;
            line = std::max(line, hint.height());
        }
        return y + line - rect.y();
    }

    QList<QLayoutItem*> items_;
    int spacing_;
};

} // namespace

Window::Window(QWidget* main, QWidget* render, BootFn boot, QString helper, QStringList helper_args)
    : QWidget(main), main_(main), render_(render), boot_(std::move(boot)),
      helper_(std::move(helper)), helper_args_(std::move(helper_args)) {
    Build();
    client_.OnLog([](const QString& line) {
        LOG_INFO(Frontend, "orm-app: {}", line.toStdString());
    });
    client_.OnEvent([this](const QString& name, const QJsonObject& event) { OnEvent(name, event); });
    client_.OnExit([this](const QString& why) {
        if (stopping_) {
            return;
        }
        SetBusy(false);
        SetLine(why + QStringLiteral(". Cierra y vuelve a abrir la app."), true);
    });
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { OnQuit(); });
    poller_ = std::make_unique<StatusPoller>(
        client_,
        [this](const QJsonObject& reply) {
            if (!stopping_ && Ok(reply)) {
                ApplyStatus(ParseStatus(reply));  // a failed poll says nothing: the next one tries
            }
        },
        [this] {
            return !stopping_ && isVisible() && client_.Running() && status_loaded_ && !busy_ &&
                   !update_busy_ && !in_game_;
        },
        kStatusTimeoutMs);
    poll_timer_ = new QTimer(this);
    poll_timer_->setInterval(kPollMs);
    connect(poll_timer_, &QTimer::timeout, this, [this] { Poll(); });
    poll_timer_->start();
    // ORM orm/pvp: the game's chat asks the app through the in-process channel
    // (orm_app_channel.h); called on the hook's thread, handled here on ours.
    QPointer<Window> self(this);
    OrmAppChannel::Listen([self](const OrmAppChannel::Request& request) {
        if (self) {
            QMetaObject::invokeMethod(
                self.data(),
                [self, request] {
                    if (self) {
                        self->OnChannel(request);
                    }
                },
                Qt::QueuedConnection);
        }
    });
    // The toggles follow the hook's state: checked often, drawn only on a change.
    auto* switch_timer = new QTimer(this);
    switch_timer->setInterval(250);
    connect(switch_timer, &QTimer::timeout, this, [this] { SyncSwitches(); });
    switch_timer->start();
    // After the main window is up, so a slow helper never delays it.
    QTimer::singleShot(0, this, [this] { StartHelper(); });
}

Window::~Window() {
    OrmAppChannel::Listen({});  // ORM orm/pvp: no more asks for this window
    stopping_ = true;
    if (client_.Running()) {
        client_.Stop(1500);
    }
}

void Window::Build() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);

    // One column over the whole central area; it grows and shrinks with the
    // window (no fixed widths), the scroll area takes over when it is small.
    auto* column = new QWidget(scroll);
    scroll->setWidget(column);
    auto* v = new QVBoxLayout(column);
    const int gap = fontMetrics().height();
    v->setContentsMargins(gap, gap, gap, gap);
    v->setSpacing(gap * 3 / 4);

    // The toast at the top. A new version: it stays until acted on, with a
    // big "Actualizar ahora" (download, install, restart: one click) and the
    // download's progress; × hides it until the next start. A helper notice:
    // its text and ×.
    toast_ = new QFrame(column);
    toast_->setObjectName(QStringLiteral("ormToast"));
    toast_->setStyleSheet(QStringLiteral(
        "#ormToast { background: #1f5f99; border-radius: 6px; } "
        "#ormToast QLabel { color: white; font-weight: bold; }"));
    auto* toast_v = new QVBoxLayout(toast_);
    auto* toast_row = new QHBoxLayout();
    toast_text_ = new QLabel(toast_);
    toast_text_->setWordWrap(true);
    toast_text_->setMinimumWidth(1);
    toast_text_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    toast_button_ = MakeButton(QStringLiteral("Actualizar ahora"), toast_);
    {
        QFont f = toast_button_->font();
        f.setBold(true);
        f.setPointSizeF(f.pointSizeF() * 1.25);
        toast_button_->setFont(f);
        toast_button_->setMinimumHeight(QFontMetrics(f).height() * 2);
        toast_button_->setMinimumWidth(QFontMetrics(f).horizontalAdvance(toast_button_->text()) +
                                       QFontMetrics(f).height() * 2);
    }
    auto* toast_close = MakeButton(QStringLiteral("×"), toast_);
    toast_close->setFlat(true);
    toast_close->setStyleSheet(QStringLiteral("QPushButton { color: white; font-weight: bold; }"));
    toast_close->setToolTip(QStringLiteral("Ocultar el aviso hasta el próximo arranque"));
    toast_row->addWidget(toast_text_, 1);
    toast_row->addWidget(toast_button_);
    toast_row->addWidget(toast_close, 0, Qt::AlignTop);
    toast_v->addLayout(toast_row);
    toast_progress_ = new QProgressBar(toast_);
    toast_progress_->setRange(0, 100);
    toast_progress_->setTextVisible(true);
    toast_progress_->hide();
    toast_v->addWidget(toast_progress_);
    toast_line_ = new QLabel(toast_);
    toast_line_->setWordWrap(true);
    toast_line_->setMinimumWidth(1);
    toast_line_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    toast_line_->hide();
    toast_v->addWidget(toast_line_);
    toast_->hide();
    connect(toast_button_, &QPushButton::clicked, this, [this] {
        if (!toast_version_.isEmpty()) {
            UpdateNow();
        } else if (staged_) {
            ApplyAndRestart();
        } else {
            Restart();  // the helper asked for one more start to finish an update
        }
    });
    connect(toast_close, &QPushButton::clicked, this, [this] {
        if (!toast_version_.isEmpty()) {
            toast_closed_ = toast_version_;  // not shown again for it until the next start
        }
        toast_->hide();
    });
    v->addWidget(toast_);

    auto* title = new QLabel(QStringLiteral("Cooperativo"), column);
    QFont big = title->font();
    big.setPointSizeF(big.pointSizeF() * 1.8);  // relative: follows the DPI scale
    big.setBold(true);
    title->setFont(big);
    auto* subtitle =
        new QLabel(QStringLiteral("The Legend of Zelda: Ocarina of Time, cooperativo"), column);
    subtitle->setWordWrap(true);
    subtitle->setMinimumWidth(1);
    v->addWidget(title);
    v->addWidget(subtitle);

    // Status rows.
    auto* state_box = new QGroupBox(QStringLiteral("Estado"), column);
    auto* grid = new QGridLayout(state_box);
    grid->setColumnStretch(1, 1);
    int r = 0;
    for (Row* row : {&keys_, &firmware_, &game_, &puppet_, &shaders_, &version_}) {
        row->mark = new QLabel(QStringLiteral("…"), state_box);
        row->mark->setMinimumWidth(fontMetrics().horizontalAdvance(QStringLiteral("✓✓")));
        row->mark->setAlignment(Qt::AlignCenter);
        row->text = new QLabel(state_box);
        row->text->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        row->text->setWordWrap(true);
        // A long path must not widen the screen: the label may get narrower
        // than its text (it wraps at spaces, the tooltip has it whole).
        row->text->setMinimumWidth(1);
        row->text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        grid->addWidget(row->mark, r, 0, Qt::AlignTop);
        grid->addWidget(row->text, r, 1);
        if (row == &keys_ || row == &firmware_ || row == &game_) {
            row->open = new QLabel(
                QStringLiteral("<a href=\"open\">Abrir carpeta</a>"), state_box);
            row->open->setTextInteractionFlags(Qt::LinksAccessibleByMouse |
                                               Qt::LinksAccessibleByKeyboard);
            row->open->hide();  // until the helper names the folder
            grid->addWidget(row->open, r, 2, Qt::AlignTop | Qt::AlignRight);
            connect(row->open, &QLabel::linkActivated, this, [this, row] { OpenFolder(*row); });
        }
        ++r;
    }
    v->addWidget(state_box);

    // Dropping the files, and "Preparar".
    auto* setup_box = new QGroupBox(QStringLiteral("Preparación"), column);
    auto* setup = new QVBoxLayout(setup_box);
    auto* hint = new QLabel(
        QStringLiteral("Arrastra aquí tus claves (prod.keys, title.keys), el firmware (.nca, su "
                       "carpeta o su .zip) y el juego (.nup, .nsp), o ponlos a mano en sus carpetas."),
        setup_box);
    hint->setWordWrap(true);
    hint->setMinimumWidth(1);
    hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    setup->addWidget(hint);
    auto* folders = new FlowLayout(gap / 2);
    prepare_button_ = MakeButton(QStringLiteral("Preparar"), setup_box);
    prepare_button_->setToolTip(QStringLiteral(
        "Construye el paquete de marioneta y prepara los shaders (empieza solo la primera vez)"));
    folders->addWidget(prepare_button_);
    retry_button_ = MakeButton(QStringLiteral("Reintentar"), setup_box);
    retry_button_->setToolTip(QStringLiteral(
        "Vuelve a intentar ahora la descarga del juego y lo demás, siguiendo donde se quedó"));
    retry_button_->hide();
    folders->addWidget(retry_button_);
    setup->addLayout(folders);
    progress_ = new QProgressBar(setup_box);
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setTextVisible(true);
    setup->addWidget(progress_);
    // What is going on, or what went wrong: wraps anywhere (long paths too),
    // can be selected and copied.
    line_ = new QTextEdit(setup_box);
    line_->setReadOnly(true);
    line_->setFrameShape(QFrame::NoFrame);
    line_->setLineWrapMode(QTextEdit::WidgetWidth);
    line_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    line_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    line_->setFixedHeight(fontMetrics().lineSpacing() * 4 + 8);  // four lines of the font
    line_->setStyleSheet(QStringLiteral("QTextEdit { background: transparent; }"));
    setup->addWidget(line_);
    v->addWidget(setup_box);
    connect(prepare_button_, &QPushButton::clicked, this, [this] { Prepare(); });
    connect(retry_button_, &QPushButton::clicked, this, [this] { RetryDownloads(); });

    // Room, name, "Jugar".
    auto* play_box = new QGroupBox(QStringLiteral("Partida"), column);
    auto* form = new QFormLayout(play_box);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    room_ = new QLineEdit(play_box);
    room_->setPlaceholderText(QStringLiteral("Nombre de la sala"));
    name_ = new QLineEdit(play_box);
    name_->setPlaceholderText(QStringLiteral("Tu nombre"));
    name_->setMaxLength(32);
    password_ = new QLineEdit(play_box);
    password_->setEchoMode(QLineEdit::Password);
    password_->setPlaceholderText(QStringLiteral("Opcional"));
    form->addRow(QStringLiteral("Sala"), room_);
    form->addRow(QStringLiteral("Nombre"), name_);
    form->addRow(QStringLiteral("Contraseña"), password_);
    // ORM one host (oot-remake docs/PARTIDA_SALA.md, "Un solo anfitrión"): off,
    // the relay picks the host by the connections; on, this player hosts once
    // its game is in (until someone types /anfitrion <nombre> in the chat).
    host_me_ = new QCheckBox(QStringLiteral("Quiero ser el anfitrión"), play_box);
    host_me_->setToolTip(QStringLiteral(
        "El anfitrión guarda la partida de la sala para todos. Sin marcar, se elige solo: "
        "el que tenga mejor conexión con los demás. En el chat: /anfitrion <nombre> lo cambia "
        "y /anfitrion auto lo devuelve a la elección automática."));
    form->addRow(QString(), host_me_);
    // ORM orm/pvp (COMANDOS.md): the room's two switches, live, as /pvp and
    // /fijar in the chat; greyed out while not in a room.
    auto* switches = new QWidget(play_box);
    auto* switches_row = new QHBoxLayout(switches);
    switches_row->setContentsMargins(0, 0, 0, 0);
    pvp_box_ = new QCheckBox(QStringLiteral("PvP"), switches);
    pvp_box_->setToolTip(QStringLiteral("Las armas de un jugador dañan a los demás (toda la sala). "
                                        "También con /pvp on|off en el chat."));
    lock_box_ = new QCheckBox(QStringLiteral("Fijar aliados"), switches);
    lock_box_->setToolTip(QStringLiteral("Fijar con Z a los compañeros como a un enemigo (toda la sala). "
                                         "También con /fijar on|off en el chat."));
    switches_row->addWidget(pvp_box_);
    switches_row->addWidget(lock_box_);
    switches_row->addStretch(1);
    form->addRow(QStringLiteral("En la sala"), switches);
    for (QCheckBox* box : {pvp_box_, lock_box_}) {
        box->setEnabled(false);
        connect(box, &QCheckBox::clicked, this, [this, box](bool on) {
            const std::uint8_t key = box == pvp_box_ ? 1 : 2;  // protocol::RoomSwitchKey
            const bool sent = OrmAppChannel::SetSwitch(key, on);
            LOG_INFO(Frontend, "orm-app: {} {} from the screen: {}", box == pvp_box_ ? "PvP" : "Fijar aliados",
                     on ? "on" : "off", sent ? "sent" : "not sent (not in a room)");
            if (!sent) {
                SetLine(QStringLiteral("Para cambiar PvP o Fijar aliados tienes que estar en una sala."), true);
            }
            switches_seen_ = ~std::uint64_t{0};  // redraw from the room's state, not from the click
            SyncSwitches();
        });
    }
    play_button_ = MakeButton(QStringLiteral("Jugar"), play_box);
    QFont bold = play_button_->font();
    bold.setBold(true);
    bold.setPointSizeF(bold.pointSizeF() * 1.25);
    play_button_->setFont(bold);
    play_button_->setMinimumHeight(QFontMetrics(bold).height() * 2);
    form->addRow(play_button_);
    v->addWidget(play_box);
    connect(play_button_, &QPushButton::clicked, this, [this] { Play(); });
    connect(room_, &QLineEdit::returnPressed, this, [this] { Play(); });
    connect(name_, &QLineEdit::returnPressed, this, [this] { Play(); });

    // Versions, update control and "Novedades".
    auto* version_box = new QGroupBox(QStringLiteral("Versión y actualizaciones"), column);
    auto* vv = new QVBoxLayout(version_box);
    versions_ = new QLabel(version_box);
    versions_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    versions_->setWordWrap(true);
    versions_->setMinimumWidth(1);
    versions_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    vv->addWidget(versions_);
    auto* update_row = new FlowLayout(gap / 2);
    check_button_ = MakeButton(QStringLiteral("Buscar"), version_box);
    check_button_->setToolTip(QStringLiteral("Buscar una versión nueva en el servidor"));
    update_button_ = MakeButton(QStringLiteral("Actualizar ahora"), version_box);
    update_button_->setToolTip(QStringLiteral("Descargar la versión nueva y reiniciar"));
    rollback_button_ = MakeButton(QStringLiteral("Versión anterior"), version_box);
    rollback_button_->setToolTip(QStringLiteral("Volver a la versión anterior y reiniciar"));
    update_button_->setEnabled(false);
    rollback_button_->setEnabled(false);
    update_row->addWidget(check_button_);
    update_row->addWidget(update_button_);
    update_row->addWidget(rollback_button_);
    vv->addLayout(update_row);
    update_line_ = new QLabel(version_box);
    update_line_->setWordWrap(true);
    update_line_->setMinimumWidth(1);
    update_line_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    update_line_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    vv->addWidget(update_line_);
    vv->addWidget(new QLabel(QStringLiteral("Novedades"), version_box));
    notes_ = new QTextBrowser(version_box);
    notes_->setOpenLinks(false);
    notes_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    notes_->setMinimumHeight(fontMetrics().lineSpacing() * 6);
    vv->addWidget(notes_, 1);
    shortcut_button_ = MakeButton(QString(), version_box);
    shortcut_button_->setFlat(true);
    shortcut_button_->setStyleSheet(
        QStringLiteral("QPushButton { color: palette(link); text-decoration: underline; "
                       "text-align: left; }"));
    shortcut_button_->hide();  // until the helper says whether the shortcut is there
    vv->addWidget(shortcut_button_, 0, Qt::AlignLeft);
    v->addWidget(version_box, 1);
    connect(check_button_, &QPushButton::clicked, this, [this] { CheckUpdate(false); });
    connect(update_button_, &QPushButton::clicked, this, [this] { UpdateNow(); });
    connect(rollback_button_, &QPushButton::clicked, this, [this] { Rollback(); });
    connect(shortcut_button_, &QPushButton::clicked, this, [this] { ToggleShortcut(); });

    // Files dropped anywhere on the screen go to the helper's `import`: no
    // child takes them (a line edit would paste the path, the main window
    // would boot a dropped game).
    setAcceptDrops(true);
    for (QWidget* child : findChildren<QWidget*>()) {
        child->setAcceptDrops(false);
    }

    ApplyStatus(status_);
    SetBusy(true, QStringLiteral("Arrancando el ayudante…"));
}

void Window::StartHelper() {
    QString error;
    if (!client_.Start(helper_, helper_args_, &error)) {
        SetBusy(false);
        SetLine(QStringLiteral("No arranca el ayudante (%1): %2. Reinstala la app con ORM-Setup.")
                    .arg(QDir::toNativeSeparators(helper_), error),
                true);
        for (QPushButton* b : {prepare_button_, play_button_, check_button_, update_button_,
                               rollback_button_, toast_button_, shortcut_button_}) {
            b->setEnabled(false);
        }
        return;
    }
    client_.Send(QStringLiteral("status"), {}, [this](const QJsonObject& reply) {
        SetBusy(false);
        if (!Ok(reply)) {
            SetLine(ErrorText(reply, QStringLiteral("El ayudante no da el estado")), true);
            return;
        }
        ApplyStatus(ParseStatus(reply));
        SetLine(QStringLiteral("Listo."));
        CheckUpdate(true);
    }, kStatusTimeoutMs);
}

void Window::Refresh() {
    if (!client_.Running()) {
        return;
    }
    client_.Send(QStringLiteral("status"), {}, [this](const QJsonObject& reply) {
        if (Ok(reply)) {
            ApplyStatus(ParseStatus(reply));
        } else {
            SetLine(ErrorText(reply, QStringLiteral("El ayudante no da el estado")), true);
        }
    }, kStatusTimeoutMs);
}

void Window::GameEnded() {
    if (!client_.Running()) {
        return;
    }
    in_game_ = false;
    SetLine(QStringLiteral("Partida terminada."));
    client_.Send(QStringLiteral("game_ended"), {}, [this](const QJsonObject& reply) {
        if (!Ok(reply)) {
            SetLine(ErrorText(reply, QStringLiteral("El ayudante no recibió el fin de la partida")),
                    true);
        }
        Refresh();
    }, kShortTimeoutMs);
}

void Window::RetryDownloads() {
    if (!client_.Running()) {
        return;
    }
    retry_button_->setEnabled(false);
    SetLine(QStringLiteral("Se vuelve a intentar la descarga…"));
    client_.Send(QStringLiteral("retry_downloads"), {}, [this](const QJsonObject& reply) {
        retry_button_->setEnabled(true);
        if (!Ok(reply)) {
            SetLine(ErrorText(reply, QStringLiteral("No se pudo volver a intentar la descarga")), true);
        }
    });
}

void Window::ApplyStatus(const Status& status) {
    retry_button_->setVisible(status.downloads_pending);
    const bool first = !status_loaded_ && !status.app_version.isEmpty();
    status_ = status;
    status_loaded_ = status_loaded_ || first;
    SetRow(keys_, QStringLiteral("Claves"), status.keys);
    SetRow(firmware_, QStringLiteral("Firmware"), status.firmware);
    SetRow(game_, QStringLiteral("Juego"), status.game);
    SetRow(puppet_, QStringLiteral("Paquete de marioneta"), status.puppet);
    SetRow(shaders_, QStringLiteral("Shaders"), status.shaders);
    Check version;
    version.known = !status.app_version.isEmpty() && !status.channel_version.isEmpty();
    version.ok = status.app_version == status.channel_version;
    version.detail = status.app_version.isEmpty() ? QString() : status.app_version;
    if (version.known && !version.ok) {
        version.detail += QStringLiteral(" (en el servidor: %1)").arg(status.channel_version);
    } else if (version.known) {
        version.detail += QStringLiteral(" (al día)");
    }
    SetRow(version_, QStringLiteral("Versión"), version);
    versions_->setText(VersionLines(status, EmulatorBuild(), OwnProtocol()).join(QLatin1Char('\n')));
    notes_->setPlainText(NotesText(status));
    if (first) {
        if (room_->text().isEmpty()) {
            room_->setText(status.room);
        }
        host_me_->setChecked(status.host == QStringLiteral("me"));
    }
    // "never": room_save_host = false in the settings, this PC never hosts.
    host_me_->setEnabled(status.host != QStringLiteral("never"));
    host_me_->setVisible(!status.host.isEmpty());  // an older helper knows nothing of it
    staged_ = staged_ || status.update_ready;
    if (version.known && !version.ok) {
        ShowUpdateToast(status.channel_version);
    } else if (status.update_ready) {
        ShowUpdateToast(status.channel_version);
    }
    update_button_->setEnabled(!update_busy_ && client_.Running() &&
                               ((version.known && !version.ok) || staged_));
    rollback_button_->setEnabled(!update_busy_ && client_.Running() && status.rollback_available);
    if (status.rollback_available && !status.previous_version.isEmpty()) {
        rollback_button_->setToolTip(
            QStringLiteral("Vuelve a la versión %1").arg(status.previous_version));
    }
    if (status.shortcut.known) {
        shortcut_button_->setText(status.shortcut.ok
                                      ? QStringLiteral("Quitar el acceso directo del escritorio")
                                      : QStringLiteral("Crear acceso directo en el escritorio"));
        shortcut_button_->setToolTip(QDir::toNativeSeparators(status.shortcut.detail));
        shortcut_button_->show();
    } else {
        shortcut_button_->hide();
    }
    // Decided once the reply that brought this status is done (busy_ may
    // still be on inside it).
    QTimer::singleShot(0, this, [this] { MaybeAutoPrepare(); });
}

void Window::SetRow(Row& row, const QString& label, const Check& check) {
    row.mark->setText(Mark(check));
    row.mark->setStyleSheet(!check.known ? QStringLiteral("color: gray;")
                            : check.ok   ? QStringLiteral("color: #2e9d4f; font-weight: bold;")
                                         : QStringLiteral("color: #d0453a; font-weight: bold;"));
    QString text = label;
    if (!check.detail.isEmpty()) {
        text += QStringLiteral(": ") + QDir::toNativeSeparators(check.detail);
    } else if (check.known && !check.ok) {
        text += QStringLiteral(": falta");
    }
    // The row always says where its files go (the helper's detail may already).
    if (!check.folder.isEmpty() &&
        !QDir::toNativeSeparators(check.detail)
             .contains(QDir::toNativeSeparators(check.folder), Qt::CaseInsensitive)) {
        text += QStringLiteral(" (carpeta: %1)").arg(QDir::toNativeSeparators(check.folder));
    }
    row.text->setText(text);
    row.text->setToolTip(text);
    if (row.open != nullptr) {
        row.folder = check.folder;
        row.open->setVisible(!check.folder.isEmpty());
        row.open->setToolTip(QDir::toNativeSeparators(check.folder));
    }
}

void Window::SetBusy(bool busy, const QString& line) {
    busy_ = busy;
    for (QPushButton* b : {prepare_button_, play_button_}) {
        b->setEnabled(!busy);
    }
    if (!line.isEmpty()) {
        SetLine(line);
    }
}

void Window::SetLine(const QString& line, bool error) {
    line_->setPlainText(line);
    line_->setStyleSheet(error ? QStringLiteral("QTextEdit { background: transparent; color: #d0453a; }")
                               : QStringLiteral("QTextEdit { background: transparent; }"));
}

void Window::SetUpdateLine(const QString& line, bool error) {
    update_line_->setText(line);
    update_line_->setStyleSheet(error ? QStringLiteral("color: #d0453a;") : QString());
}

void Window::OnEvent(const QString& name, const QJsonObject& event) {
    if (name == QStringLiteral("progress")) {
        const int pct = ProgressPct(event);
        const QString task = event.value(QStringLiteral("task")).toString();
        const QString text = ProgressText(event);
        if (task == QStringLiteral("update") || task == QStringLiteral("update_download")) {
            SetUpdateLine(text);
            const QString transfer = TransferText(event);
            SetToastProgress(pct, transfer.isEmpty() ? event.value(QStringLiteral("text")).toString()
                                                     : transfer);
            return;
        }
        if (pct >= 0) {
            progress_->setRange(0, 100);
            progress_->setValue(pct);
        } else {
            progress_->setRange(0, 0);  // busy, no percent
        }
        SetLine(text);
    } else if (ShowsSaved(name, event)) {
        CoopNotices::ShowSaved();  // "Guardado" on the game's HUD (coop_notices.h)
    } else if (name == QStringLiteral("toast")) {
        const QString text = event.value(QStringLiteral("text")).toString();
        const QString version = event.value(QStringLiteral("version")).toString().trimmed();
        if (!version.isEmpty() && version != status_.app_version) {
            // "Hay una versión nueva": the update toast, with its button.
            status_.channel_version = version;
            ShowUpdateToast(version);
            if (!text.isEmpty()) {
                toast_text_->setText(text);
            }
        } else if (!text.isEmpty()) {
            ShowToast(text, IsUpdateToast(event));
        }
    }
    // "log" is already in the emulator's log (HelperClient).
}

void Window::ShowToast(const QString& text, bool update) {
    // A plain notice never covers the update toast.
    if (!toast_version_.isEmpty() && toast_->isVisible() && !update) {
        SetUpdateLine(text);
        return;
    }
    toast_version_.clear();
    toast_text_->setText(text);
    toast_button_->setText(QStringLiteral("Reiniciar"));
    toast_button_->setVisible(update);
    toast_button_->setEnabled(true);
    toast_progress_->hide();
    toast_line_->hide();
    toast_->show();
}

void Window::ShowUpdateToast(const QString& version) {
    if (version.isEmpty() || version == status_.app_version) {
        return;
    }
    if (toast_closed_ == version) {
        return;  // × hid it: until the next start
    }
    const bool same = toast_version_ == version && toast_->isVisible();
    toast_version_ = version;
    if (!same) {
        toast_text_->setText(
            staged_ ? QStringLiteral("La versión %1 está descargada: pulsa \"Actualizar ahora\" "
                                     "y la app se reinicia con ella.")
                          .arg(version)
                    : QStringLiteral("Hay una versión nueva de la app: %1.").arg(version));
    }
    toast_button_->setText(QStringLiteral("Actualizar ahora"));
    toast_button_->setVisible(true);
    toast_button_->setEnabled(!want_update_);
    toast_progress_->setVisible(downloading_);
    toast_line_->setVisible(downloading_ || want_update_);
    toast_->show();
}

void Window::SetToastProgress(int pct, const QString& text) {
    if (toast_version_.isEmpty()) {
        return;
    }
    if (pct >= 0) {
        toast_progress_->setRange(0, 100);
        toast_progress_->setValue(pct);
    } else {
        toast_progress_->setRange(0, 0);
    }
    toast_progress_->show();
    if (!text.trimmed().isEmpty()) {
        toast_line_->setText(text.trimmed());
        toast_line_->show();
    }
}

void Window::UpdateNow() {
    if (!client_.Running()) {
        return;
    }
    want_update_ = true;
    toast_button_->setEnabled(false);
    update_button_->setEnabled(false);
    toast_line_->setText(staged_ ? QStringLiteral("Instalando la actualización…")
                                 : QStringLiteral("Descargando la versión nueva…"));
    toast_line_->show();
    if (update_busy_) {
        return;  // a check or the download in the background: it restarts when that ends
    }
    DownloadUpdate(true);
}

void Window::dragEnterEvent(QDragEnterEvent* event) {
    if (!DragPaths(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
    }
}

void Window::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData() != nullptr && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void Window::dropEvent(QDropEvent* event) {
    const QStringList paths = DragPaths(event->mimeData());
    if (paths.isEmpty()) {
        return;
    }
    event->acceptProposedAction();
    // Not inside the drop: the source (Explorer) waits for it to return.
    QTimer::singleShot(0, this, [this, paths] { Import(paths); });
}

void Window::Import(const QStringList& paths) {
    if (!client_.Running()) {
        return;
    }
    if (busy_ || in_game_) {
        SetLine(QStringLiteral("Espera a que termine lo que está en marcha y vuelve a soltar los "
                               "archivos."),
                true);
        return;
    }
    SetBusy(true, paths.size() == 1
                      ? QStringLiteral("Llevando %1 a la carpeta de la app…")
                            .arg(QDir::toNativeSeparators(paths.first()))
                      : QStringLiteral("Llevando %1 archivos o carpetas a la carpeta de la app…")
                            .arg(paths.size()));
    progress_->setRange(0, 0);
    QJsonArray list;
    for (const QString& path : paths) {
        list.append(path);
    }
    client_.Send(QStringLiteral("import"), QJsonObject{{QStringLiteral("paths"), list}},
                 [this](const QJsonObject& reply) {
                     progress_->setRange(0, 100);
                     if (!Ok(reply)) {
                         SetBusy(false);
                         SetLine(ErrorText(reply, QStringLiteral("No se reconoce nada de lo que "
                                                                 "soltaste")),
                                 true);
                         Refresh();
                         return;
                     }
                     progress_->setValue(100);
                     const ImportInfo info = ParseImport(reply);
                     SetLine(info.summary.isEmpty() ? QStringLiteral("Archivos colocados.")
                                                    : info.summary);
                     // The question is not asked inside the reply (a nested
                     // event loop there would read the helper's next lines).
                     QTimer::singleShot(0, this, [this, info] { FinishImport(info); });
                 });
}

void Window::FinishImport(const ImportInfo& info) {
    if (info.ask.isEmpty()) {
        SetBusy(false);
        Refresh();
        return;
    }
    QMessageBox box(QMessageBox::Question, QStringLiteral("Borrar los originales"), info.ask,
                    QMessageBox::Yes | QMessageBox::No, this);
    box.setDefaultButton(QMessageBox::Yes);
    box.button(QMessageBox::Yes)->setText(QStringLiteral("Sí"));
    box.button(QMessageBox::No)->setText(QStringLiteral("No"));
    const bool erase = box.exec() == QMessageBox::Yes;
    QJsonObject fields{{QStringLiteral("import_id"), info.id},
                       {QStringLiteral("delete"), erase}};
    client_.Send(QStringLiteral("import_finish"), fields, [this](const QJsonObject& reply) {
        SetBusy(false);
        if (Ok(reply)) {
            const QString summary = reply.value(QStringLiteral("summary")).toString().trimmed();
            if (!summary.isEmpty()) {
                SetLine(summary);
            }
        } else {
            SetLine(ErrorText(reply, QStringLiteral("No se pudieron borrar los originales")), true);
        }
        Refresh();
    }, kCheckTimeoutMs);
}

void Window::OpenFolder(const Row& row) {
    if (row.folder.isEmpty()) {
        return;
    }
    QDir().mkpath(row.folder);
    QDesktopServices::openUrl(QUrl::fromLocalFile(row.folder));
}

void Window::SyncSwitches() {
    if (pvp_box_ == nullptr || lock_box_ == nullptr) {
        return;
    }
    const std::uint64_t version = OrmAppChannel::SwitchesVersion();
    if (version == switches_seen_) {
        return;
    }
    switches_seen_ = version;
    const auto now = OrmAppChannel::CurrentSwitches();
    for (QCheckBox* box : {pvp_box_, lock_box_}) {
        box->blockSignals(true);
    }
    pvp_box_->setChecked(now.pvp);
    lock_box_->setChecked(now.lock_mates);
    pvp_box_->setEnabled(now.connected);
    lock_box_->setEnabled(now.connected);
    lock_box_->setText(now.lock_available ? QStringLiteral("Fijar aliados")
                                          : QStringLiteral("Fijar aliados (no disponible)"));
    for (QCheckBox* box : {pvp_box_, lock_box_}) {
        box->blockSignals(false);
    }
}

void Window::OnChannel(const OrmAppChannel::Request& request) {
    using OrmAppChannel::Ask;
    if (!client_.Running()) {
        LOG_INFO(Frontend, "orm-app: the game asked {} but the helper is not running", static_cast<int>(request.ask));
        OrmAppChannel::Answer("La app no tiene su ayudante en marcha: ciérrala y vuelve a abrirla");
        return;
    }
    switch (request.ask) {
    case Ask::upload_logs:
        LOG_INFO(Frontend, "orm-app: /registros from the game: upload_logs");
        client_.Send(QStringLiteral("upload_logs"), {}, [](const QJsonObject& reply) {
            OrmAppChannel::Answer(Ok(reply) ? std::string("Registros enviados")
                                            : "No se pudieron enviar los registros: " +
                                                  ErrorText(reply, QStringLiteral("sin respuesta")).toStdString());
        });
        return;
    case Ask::retry_downloads:
        LOG_INFO(Frontend, "orm-app: /reintentar from the game: retry_downloads");
        client_.Send(QStringLiteral("retry_downloads"), {}, [](const QJsonObject& reply) {
            OrmAppChannel::Answer(Ok(reply) ? std::string("La app vuelve a intentar las descargas")
                                            : "La app no pudo reintentar las descargas: " +
                                                  ErrorText(reply, QStringLiteral("sin respuesta")).toStdString());
        });
        return;
    case Ask::save_name: {
        const QString name = QString::fromStdString(request.text);
        LOG_INFO(Frontend, "orm-app: /nombre from the game: room name {}", request.text);
        // The game took it: the screen and the next start use it, and it is no
        // reason to restart (Play compares with coop_name_).
        name_->setText(name);
        coop_name_ = name;
        client_.Send(QStringLiteral("room"), QJsonObject{{QStringLiteral("name"), name}},
                     [name](const QJsonObject& reply) {
                         OrmAppChannel::Answer(Ok(reply) ? "La app guarda tu nombre nuevo: " + name.toStdString()
                                                         : "La app no pudo guardar el nombre: " +
                                                               ErrorText(reply, QStringLiteral("sin respuesta"))
                                                                   .toStdString());
                     });
        return;
    }
    }
}

void Window::Poll() {
    if (poller_) {
        poller_->Tick();
    }
}

void Window::MaybeAutoPrepare() {
    if (auto_prepared_ || busy_ || update_busy_ || want_update_ || in_game_ || stopping_ ||
        !client_.Running() || !ShouldAutoPrepare(status_)) {
        return;
    }
    auto_prepared_ = true;  // once per run; the button redoes it
    LOG_INFO(Frontend, "orm-app: claves, firmware y juego listos: Preparar empieza solo");
    Prepare();
}

void Window::ToggleShortcut() {
    if (!client_.Running() || !status_.shortcut.known) {
        return;
    }
    const QString action = status_.shortcut.ok ? QStringLiteral("remove") : QStringLiteral("create");
    shortcut_button_->setEnabled(false);
    client_.Send(QStringLiteral("shortcut"), QJsonObject{{QStringLiteral("action"), action}},
                 [this](const QJsonObject& reply) {
                     shortcut_button_->setEnabled(true);
                     if (!Ok(reply)) {
                         SetUpdateLine(ErrorText(reply, QStringLiteral("No se pudo cambiar el "
                                                                       "acceso directo")),
                                       true);
                     }
                     Refresh();
                 }, kShortTimeoutMs);
}

void Window::Prepare() {
    SetBusy(true, QStringLiteral("Construyendo el paquete de marioneta…"));
    progress_->setRange(0, 100);
    progress_->setValue(0);
    client_.Send(QStringLiteral("build_puppet_pack"), {}, [this](const QJsonObject& reply) {
        if (!Ok(reply)) {
            SetBusy(false);
            progress_->setRange(0, 100);
            SetLine(ErrorText(reply, QStringLiteral("No se pudo construir el paquete de marioneta")),
                    true);
            Refresh();
            return;
        }
        progress_->setValue(0);
        SetLine(QStringLiteral("Preparando los shaders…"));
        client_.Send(QStringLiteral("prepare_shaders"), {}, [this](const QJsonObject& r) {
            SetBusy(false);
            progress_->setRange(0, 100);
            if (Ok(r)) {
                progress_->setValue(100);
                SetLine(QStringLiteral("Todo preparado."));
            } else {
                SetLine(ErrorText(r, QStringLiteral("No se pudieron preparar los shaders")), true);
            }
            Refresh();
        });
    });
}

void Window::Play() {
    if (busy_) {
        return;
    }
    const QString room = room_->text().trimmed();
    const QString name = name_->text().trimmed();
    if (name.isEmpty()) {
        SetLine(QStringLiteral("Escribe tu nombre antes de jugar."), true);
        name_->setFocus();
        return;
    }
    if (room.isEmpty()) {
        SetLine(QStringLiteral("Escribe el nombre de la sala."), true);
        room_->setFocus();
        return;
    }
    if (coop_started_ && (room != coop_room_ || name != coop_name_)) {
        // Co-op reads its room once, when it starts (CoopHook::Start).
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Cambiar de sala"),
            QStringLiteral("Para entrar en otra sala o con otro nombre la app se reinicia. "
                           "¿Reiniciar ahora?"));
        if (answer != QMessageBox::Yes) {
            return;
        }
    }
    QJsonObject fields;
    fields.insert(QStringLiteral("room"), room);
    fields.insert(QStringLiteral("name"), name);
    fields.insert(QStringLiteral("password"), password_->text());
    if (!host_me_->isHidden() && host_me_->isEnabled()) {  // ORM one host (the next game takes it)
        fields.insert(QStringLiteral("host"), host_me_->isChecked() ? QStringLiteral("me") : QStringLiteral("auto"));
    }
    SetBusy(true, QStringLiteral("Entrando en la sala…"));
    client_.Send(QStringLiteral("room"), fields, [this, room, name](const QJsonObject& reply) {
        if (!Ok(reply)) {
            SetBusy(false);
            SetLine(ErrorText(reply, QStringLiteral("No se pudo entrar en la sala")), true);
            return;
        }
        if (coop_started_ && (room != coop_room_ || name != coop_name_)) {
            SetBusy(false);
            Restart();  // the helper has the new room: the next start uses it
            return;
        }
        SetLine(QStringLiteral("Preparando la partida…"));
        client_.Send(QStringLiteral("play"), {}, [this, room, name](const QJsonObject& r) {
            SetBusy(false);
            if (!Ok(r)) {
                SetLine(ErrorText(r, QStringLiteral("No se pudo preparar la partida")), true);
                return;
            }
            const PlayInfo play = ParsePlay(r);
            if (play.game.isEmpty()) {
                SetLine(QStringLiteral("El ayudante no dice qué juego arrancar."), true);
                return;
            }
            if (!coop_started_) {
                coop_room_ = room;
                coop_name_ = name;
            }
            BootFromPlay(play);
        }, kPlayTimeoutMs);
    }, kShortTimeoutMs);
}

void Window::BootFromPlay(const PlayInfo& play) {
    if (!coop_started_) {
        // The environment first: co-op and the emulator's game load read it.
        for (const auto& [key, value] : play.env) {
            SetEnv(key, value);
        }
        LOG_INFO(Frontend, "orm-app: play {} ({} variables)", play.game.toStdString(),
                 play.env.size());
        coop_started_ = true;
        if (main_ && render_) {
            CoopWindow::Attach(main_, render_);
        }
    }
    in_game_ = true;  // no polling and no "Preparar" on its own until game_ended
    SetLine(QStringLiteral("Arrancando el juego…"));
    if (boot_) {
        boot_(play.game, play.fullscreen);
    }
}

void Window::UpdateFailed(const QString& text) {
    want_update_ = false;
    downloading_ = false;
    SetUpdateLine(text, true);
    toast_progress_->hide();
    if (!toast_version_.isEmpty()) {
        toast_line_->setText(text);
        toast_line_->show();
        toast_button_->setEnabled(true);
    }
    ApplyStatus(status_);
}

void Window::CheckUpdate(bool automatic) {
    if (update_busy_ || !client_.Running()) {
        return;
    }
    update_busy_ = true;
    check_button_->setEnabled(false);
    SetUpdateLine(QStringLiteral("Buscando actualización…"));
    client_.Send(QStringLiteral("update_check"), {}, [this, automatic](const QJsonObject& reply) {
        update_busy_ = false;
        check_button_->setEnabled(true);
        if (!Ok(reply)) {
            UpdateFailed(ErrorText(reply, QStringLiteral("No se pudo consultar el servidor")));
            return;
        }
        const UpdateInfo update = ParseUpdate(reply);
        if (!update.version.isEmpty()) {
            status_.channel_version = update.version;
        }
        if (update.available && !update.notes.isEmpty()) {
            status_.notes_available = update.notes;
        }
        if (!update.available) {
            want_update_ = false;
            SetUpdateLine(QStringLiteral("Estás al día (%1).").arg(status_.app_version));
            ApplyStatus(status_);
            return;
        }
        staged_ = staged_ || update.ready;
        ApplyStatus(status_);  // the update toast, with "Actualizar ahora"
        if (want_update_) {
            DownloadUpdate(true);  // pressed while the check was out
        } else if (staged_) {
            SetUpdateLine(QStringLiteral("Versión %1 descargada: pulsa \"Actualizar ahora\".")
                              .arg(update.version));
        } else if (automatic) {
            DownloadUpdate(false);  // in the background; the toast shows its progress
        } else {
            SetUpdateLine(QStringLiteral("Hay una versión nueva: %1. Pulsa \"Actualizar ahora\".")
                              .arg(update.version));
        }
    }, kCheckTimeoutMs);
}

void Window::DownloadUpdate(bool restart_after) {
    if (!client_.Running()) {
        return;
    }
    if (restart_after) {
        want_update_ = true;
    }
    if (update_busy_) {
        return;  // the download (or a check) is out: want_update_ restarts after it
    }
    if (staged_) {
        if (want_update_) {
            ApplyAndRestart();
        }
        return;
    }
    update_busy_ = true;
    downloading_ = true;
    check_button_->setEnabled(false);
    update_button_->setEnabled(false);
    rollback_button_->setEnabled(false);
    SetUpdateLine(QStringLiteral("Descargando la versión %1…").arg(status_.channel_version));
    ShowUpdateToast(status_.channel_version);
    SetToastProgress(0, QStringLiteral("Descargando la versión %1…").arg(status_.channel_version));
    client_.Send(QStringLiteral("update_download"), {}, [this](const QJsonObject& r) {
        update_busy_ = false;
        downloading_ = false;
        check_button_->setEnabled(true);
        if (!Ok(r)) {
            UpdateFailed(ErrorText(r, QStringLiteral("No se pudo descargar la actualización")));
            return;
        }
        staged_ = true;
        toast_progress_->hide();
        if (want_update_) {
            ApplyAndRestart();
            return;
        }
        ApplyStatus(status_);
        SetUpdateLine(QStringLiteral("Versión %1 descargada: pulsa \"Actualizar ahora\".")
                          .arg(status_.channel_version));
        toast_line_->setText(QStringLiteral("Descargada: pulsa \"Actualizar ahora\" y la app se "
                                            "reinicia con ella."));
        toast_line_->show();
    });
}

void Window::ApplyAndRestart() {
    if (update_busy_ || !client_.Running()) {
        return;
    }
    update_busy_ = true;
    SetUpdateLine(QStringLiteral("Instalando la actualización…"));
    if (toast_->isVisible()) {
        toast_line_->setText(QStringLiteral("Instalando la actualización y reiniciando…"));
        toast_line_->show();
        toast_button_->setEnabled(false);
    }
    client_.Send(QStringLiteral("update_apply_on_restart"), {}, [this](const QJsonObject& r) {
        update_busy_ = false;
        if (!Ok(r)) {
            UpdateFailed(ErrorText(r, QStringLiteral("No se pudo instalar la actualización")));
            return;
        }
        applied_ = true;
        SetUpdateLine(QStringLiteral("Reiniciando…"));
        Restart();
    }, kCheckTimeoutMs);
}

void Window::Rollback() {
    if (update_busy_ || !client_.Running()) {
        return;
    }
    const QString previous = status_.previous_version.isEmpty()
                                 ? QStringLiteral("anterior")
                                 : status_.previous_version;
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Volver a la versión anterior"),
        QStringLiteral("La app vuelve a la versión %1 y se reinicia. ¿Seguir?").arg(previous));
    if (answer != QMessageBox::Yes) {
        return;
    }
    update_busy_ = true;
    SetUpdateLine(QStringLiteral("Volviendo a la versión %1…").arg(previous));
    client_.Send(QStringLiteral("update_rollback"), {}, [this](const QJsonObject& r) {
        update_busy_ = false;
        if (!Ok(r)) {
            SetUpdateLine(ErrorText(r, QStringLiteral("No se pudo volver a la versión anterior")),
                          true);
            ApplyStatus(status_);
            return;
        }
        applied_ = true;  // nothing more to apply on the way out
        staged_ = false;
        SetUpdateLine(QStringLiteral("Reiniciando…"));
        Restart();
    }, kCheckTimeoutMs);
}

void Window::Restart() {
    stopping_ = true;
    const QString exe = QCoreApplication::applicationFilePath();
    const QStringList args = QCoreApplication::arguments().mid(1);
    client_.Stop();
    if (main_ && !main_->close()) {
        // The main window refused (a game running): stay, nothing restarted.
        stopping_ = false;
        want_update_ = false;
        toast_button_->setEnabled(true);
        SetUpdateLine(QStringLiteral("Cierra el juego y vuelve a pulsar Reiniciar."), true);
        StartHelper();
        return;
    }
    // The new emulator must not start while this one is still closing (it holds
    // the log and the user folder, and the new one quit at once: 0.11.7 ->
    // 0.11.8). "OoT Coop.exe" at the app's top waits for this pid to end, starts
    // emulator\lichii.exe and starts it once more if it is not up 10 s later
    // (its lines go to launcher.log). The pid goes in the environment: an older
    // OoT Coop.exe (after a rollback) ignores it and just starts the emulator.
    const QDir emulator_dir(QCoreApplication::applicationDirPath());
    const QString stub = emulator_dir.absoluteFilePath(QStringLiteral("../OoT Coop.exe"));
    if (QFileInfo(stub).isFile()) {
        const qint64 pid = QCoreApplication::applicationPid();
        QProcess waiter;
        waiter.setProgram(stub);
        waiter.setArguments(args);
        waiter.setWorkingDirectory(QFileInfo(stub).absolutePath());
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("ORM_RESTART_WAIT_PID"), QString::number(pid));
        waiter.setProcessEnvironment(env);
        qint64 waiter_pid = 0;
        if (waiter.startDetached(&waiter_pid)) {
            LOG_INFO(Frontend, "orm-app: restarting through {} (waits for pid {}; waiter pid {})",
                     QDir::toNativeSeparators(stub).toStdString(), pid, waiter_pid);
            return;
        }
        LOG_WARNING(Frontend, "orm-app: {} did not start: restarting {} directly",
                    QDir::toNativeSeparators(stub).toStdString(), exe.toStdString());
    }
    LOG_INFO(Frontend, "orm-app: restarting {}", exe.toStdString());
    QProcess::startDetached(exe, args, QCoreApplication::applicationDirPath());
}

void Window::OnQuit() {
    if (stopping_ && !client_.Running()) {
        return;
    }
    stopping_ = true;
    if (!client_.Running()) {
        return;
    }
    if (staged_ && !applied_) {
        // "se instalará al reiniciar": the next start is the new version. The
        // helper answers requests in order, so this is done before `quit`.
        client_.Send(QStringLiteral("update_apply_on_restart"), {}, [](const QJsonObject&) {});
    }
    client_.Stop();
}

} // namespace OrmApp
