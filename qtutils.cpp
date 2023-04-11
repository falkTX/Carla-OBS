/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qtutils.h"

#include <QtCore/QTimer>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>

void carla_obs_callback_on_main_thread(void (*callback)(void *param), void *param)
{
    QTimer *const maintimer = new QTimer;
    maintimer->moveToThread(qApp->thread());
    maintimer->setSingleShot(true);
    QObject::connect(maintimer, &QTimer::timeout, [maintimer, callback, param]() {
        callback(param);
        maintimer->deleteLater();
    });
    QMetaObject::invokeMethod(maintimer, "start", Qt::QueuedConnection, Q_ARG(int, 0));
}

uintptr_t carla_obs_get_main_window_id(void)
{
    const QWidgetList &wl = QApplication::topLevelWidgets();

    for (QWidget *w : wl)
    {
        if (QMainWindow *mw = qobject_cast<QMainWindow*>(w))
            return mw->winId();
    }

    for (QWidget *w : wl)
    {
        if (!w->parent())
            return w->winId();
    }

    return wl[0]->winId();
}
