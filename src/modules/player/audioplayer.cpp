#include "audioplayer.h"
#include <QDebug>

AudioPlayer::AudioPlayer(QObject *parent) : QObject(parent),
    isPlaying(false)
{
    audioOutput = new QAudioOutput(this);
    audioOutput->setVolume(1.0); // Full volume (1.0 = 100%)

    player = new QMediaPlayer(this);
    player->setAudioOutput(audioOutput);

    connect(player, &QMediaPlayer::mediaStatusChanged, this, [](QMediaPlayer::MediaStatus status) {
        qDebug() << "Media status changed:" << status;
        if (status == QMediaPlayer::LoadedMedia)
            qDebug() << "Media loaded successfully.";
    });

    connect(player, &QMediaPlayer::errorOccurred, this, [](QMediaPlayer::Error error, const QString &errorString) {
        qDebug() << "Playback error:" << errorString;
    });

    connect(player, &QMediaPlayer::playbackStateChanged, this, [](QMediaPlayer::PlaybackState state) {
        qDebug() << "Playback state changed:" << state;
    });

    // Some backends don't actually start playback if play() is called before
    // the newly-set source has finished loading. Retry once loading catches up.
    connect(player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (m_pendingPlay && (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia)) {
            m_pendingPlay = false;
            player->play();
        }
    });
}

void AudioPlayer::togglePlayPause()
{
    if (!isPlaying) {
        qDebug() << "Attempting to play:" << currSong;
        player->play();
        isPlaying = true;
    } else {
        qDebug() << "Pausing playback.";
        player->pause();
        isPlaying = false;
    }
}

void AudioPlayer::setCurrentSong(const QString &currSong){
    this->currSong = currSong;
}

void AudioPlayer::setVolume(float volume){
    audioOutput->setVolume(volume);
}

QMediaPlayer* AudioPlayer::getPlayer() const
{
    return player;
}

void AudioPlayer::play(){
    qDebug() << "Attempting to play:" << currSong;
    // Switching the source while the pipeline is still playing/loading the
    // previous track can leave some backends stuck "paused" until something
    // else (like a seek) nudges them - stopping first guarantees a clean
    // pipeline for the new source, so playback actually starts immediately.
    player->stop();
    m_pendingPlay = true;
    player->setSource(QUrl::fromLocalFile(currSong));
    player->play();
    // A seek right after play() reliably "wakes up" backends that otherwise
    // sit there loaded-but-silent until the user manually drags the seek
    // bar - this does programmatically what that manual workaround did.
    player->setPosition(0);
    isPlaying = true;
}

void AudioPlayer::stopAndClear()
{
    m_pendingPlay = false;
    player->stop();
    player->setSource(QUrl());
    currSong.clear();
    isPlaying = false;
}

