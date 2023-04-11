/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qtutils.h"

#include <QtCore/QTimer>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>

static void carla_obs_run_on_main_thread(std::function<void()> function)
{
    QTimer *const maintimer = new QTimer;
    maintimer->moveToThread(qApp->thread());
    maintimer->setSingleShot(true);
    QObject::connect(maintimer, &QTimer::timeout, [=]() {
        function();
        maintimer->deleteLater();
    });
    QMetaObject::invokeMethod(maintimer, "start", Qt::QueuedConnection, Q_ARG(int, 0));
}

struct carla_obs_idle_callback_data
{
    QTimer* timer;
};

carla_obs_idle_callback_data_t* carla_obs_add_idle_callback(carla_obs_idle_callback_t cb, void *data)
{
    carla_obs_idle_callback_data* const cbdata = new carla_obs_idle_callback_data;
    cbdata->timer = new QTimer;
    cbdata->timer->moveToThread(qApp->thread());

    QObject::connect(cbdata->timer, &QTimer::timeout, [cb, data]() {
        cb(data);
    });

    cbdata->timer->start(1000/30);

    return cbdata;
}

void carla_obs_remove_idle_callback(carla_obs_idle_callback_data_t* cbdata)
{
    QTimer *const timer = cbdata->timer;
    carla_obs_run_on_main_thread([timer](){
        timer->stop();
        timer->deleteLater();
    });
    delete cbdata;
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
