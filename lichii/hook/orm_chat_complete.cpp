// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM suggestions in the chat box (orm_chat_complete.h).

#include "orm_chat_complete.h"

#include <string>
#include <vector>

#include <QEvent>
#include <QFont>
#include <QPoint>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QPointer>
#include <QString>

#include "coop/host/orm_commands.hpp"
#include "orm_commands_game.h"

namespace OrmChatComplete {
namespace {

namespace cmd = coop::host::orm::commands;

constexpr int kMaxRows = 10;  // the list shows at most this many (scrolls with the selection)

QString Escape(const std::string& text) {
    return QString::fromStdString(text).toHtmlEscaped();
}

class Suggestions final : public QObject {
public:
    explicit Suggestions(QLineEdit* edit) : QObject(edit), edit_(edit) {
        list_ = new QLabel(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
        list_->setAttribute(Qt::WA_ShowWithoutActivating);
        list_->setTextFormat(Qt::RichText);
        list_->setStyleSheet(QStringLiteral("QLabel { background: rgba(0,0,0,215); color: #ddd;"
                                            " border: 1px solid #888; border-radius: 4px; padding: 4px; }"));
        QFont f = edit->font();
        f.setPixelSize(13);
        list_->setFont(f);
        connect(edit, &QLineEdit::textEdited, this, [this](const QString& text) {
            const std::string typed = text.toStdString();
            engine_.typed(typed, OrmCommands::Suggest(typed));
            Show();
        });
        connect(edit, &QLineEdit::textChanged, this, [this](const QString& text) {
            if (text.isEmpty()) {  // sent or cleared: the box closes or starts over
                engine_.clear();
                list_->hide();
            }
        });
        edit->installEventFilter(this);
    }
    ~Suggestions() override {
        delete list_;
    }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (obj != edit_) {
            return false;
        }
        if (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide) {
            list_->hide();
            return false;
        }
        if (event->type() != QEvent::KeyPress) {
            return false;
        }
        const auto* key = static_cast<QKeyEvent*>(event);
        const bool command = edit_->text().startsWith(QLatin1Char('/'));
        switch (key->key()) {
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            if (!command) {
                return false;
            }
            if (const auto text = engine_.tab()) {
                edit_->setText(QString::fromStdString(*text));  // not textEdited: the list stays
                edit_->setCursorPosition(edit_->text().size());
                Show();
            }
            return true;  // never a focus change while typing a command
        case Qt::Key_Up:
        case Qt::Key_Down:
            if (!command || engine_.list().empty()) {
                return false;
            }
            engine_.move(key->key() == Qt::Key_Down ? 1 : -1);
            Show();
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (const auto text = engine_.enter()) {
                edit_->setText(QString::fromStdString(*text));  // the box's Enter sends it
            }
            list_->hide();
            return false;
        default:
            return false;
        }
    }

private:
    void Show() {
        const auto& items = engine_.list();
        if (items.empty() || !edit_ || !edit_->isVisible()) {
            list_->hide();
            return;
        }
        const int n = static_cast<int>(items.size());
        const int selected = engine_.selected();
        const int first = selected >= kMaxRows ? selected - kMaxRows + 1 : 0;
        QString html;
        for (int i = first; i < n && i < first + kMaxRows; ++i) {
            const auto& s = items[static_cast<std::size_t>(i)];
            const bool on = i == selected || (selected < 0 && i == 0);
            QString row = QStringLiteral("<span style='color:%1'>%2</span>")
                              .arg(on ? QStringLiteral("#ffd23c") : QStringLiteral("#ffffff"), Escape(s.label));
            if (!s.help.empty()) {
                row += QStringLiteral(" <span style='color:#aaa'>%1</span>").arg(Escape(s.help));
            }
            html += (html.isEmpty() ? QString() : QStringLiteral("<br>")) + (on ? QStringLiteral("<b>") + row + QStringLiteral("</b>") : row);
        }
        if (n > kMaxRows) {
            html += QStringLiteral("<br><span style='color:#888'>(%1 de %2)</span>").arg(selected + 1).arg(n);
        }
        list_->setText(html);
        list_->adjustSize();
        const QPoint corner = edit_->mapToGlobal(QPoint(0, 0));
        list_->move(corner.x(), corner.y() - list_->height() - 2);  // over the box (it sits at the bottom)
        list_->show();
        list_->raise();
    }

    QPointer<QLineEdit> edit_;
    QLabel* list_ = nullptr;
    cmd::Completer engine_;
};

} // namespace

void Attach(QLineEdit* edit) {
    if (edit != nullptr) {
        new Suggestions(edit);  // owned by `edit`
    }
}

} // namespace OrmChatComplete
