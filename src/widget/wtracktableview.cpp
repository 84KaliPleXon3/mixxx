#include <QModelIndex>
#include <QInputDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QDrag>
#include <QShortcut>

#include "widget/wtracktableview.h"

#include "widget/wcoverartmenu.h"
#include "widget/wskincolor.h"
#include "widget/wtracktableviewheader.h"
#include "widget/wwidget.h"
#include "library/coverartcache.h"
#include "library/dlgtrackinfo.h"
#include "library/librarytablemodel.h"
#include "library/crate/cratefeaturehelper.h"
#include "library/dao/trackschema.h"
#include "control/controlobject.h"
#include "control/controlproxy.h"
#include "track/track.h"
#include "track/trackref.h"
#include "sources/soundsourceproxy.h"
#include "mixer/playermanager.h"
#include "preferences/dialog/dlgpreflibrary.h"
#include "waveform/guitick.h"
#include "util/dnd.h"
#include "util/time.h"
#include "util/assert.h"

WTrackTableView::WTrackTableView(QWidget * parent,
                                 UserSettingsPointer pConfig,
                                 TrackCollection* pTrackCollection, bool sorting)
        : WLibraryTableView(parent, pConfig,
                            ConfigKey(LIBRARY_CONFIGVALUE,
                                      WTRACKTABLEVIEW_VSCROLLBARPOS_KEY)),
          m_pConfig(pConfig),
          m_pTrackCollection(pTrackCollection),
          m_sorting(sorting),
          m_iCoverSourceColumn(-1),
          m_iCoverTypeColumn(-1),
          m_iCoverLocationColumn(-1),
          m_iCoverHashColumn(-1),
          m_iCoverColumn(-1),
          m_selectionChangedSinceLastGuiTick(true),
          m_loadCachedOnly(false) {


    connect(&m_loadTrackMapper, SIGNAL(mapped(QString)),
            this, SLOT(loadSelectionToGroup(QString)));

    connect(&m_deckMapper, SIGNAL(mapped(QString)),
            this, SLOT(loadSelectionToGroup(QString)));
    connect(&m_samplerMapper, SIGNAL(mapped(QString)),
            this, SLOT(loadSelectionToGroup(QString)));
    connect(&m_BpmMapper, SIGNAL(mapped(int)),
            this, SLOT(slotScaleBpm(int)));

    m_pNumSamplers = new ControlProxy(
            "[Master]", "num_samplers", this);
    m_pNumDecks = new ControlProxy(
            "[Master]", "num_decks", this);
    m_pNumPreviewDecks = new ControlProxy(
            "[Master]", "num_preview_decks", this);

    m_pMenu = new QMenu(this);

    m_pSamplerMenu = new QMenu(this);
    m_pSamplerMenu->setTitle(tr("Load to Sampler"));
    m_pPlaylistMenu = new QMenu(this);
    m_pPlaylistMenu->setTitle(tr("Add to Playlist"));
    m_pCrateMenu = new QMenu(this);
    m_pCrateMenu->setTitle(tr("Add to Crate"));
    m_pBPMMenu = new QMenu(this);
    m_pBPMMenu->setTitle(tr("BPM Options"));
    m_pCoverMenu = new WCoverArtMenu(this);
    m_pCoverMenu->setTitle(tr("Cover Art"));
    connect(m_pCoverMenu, SIGNAL(coverInfoSelected(const CoverInfo&)),
            this, SLOT(slotCoverInfoSelected(const CoverInfo&)));
    connect(m_pCoverMenu, SIGNAL(reloadCoverArt()),
            this, SLOT(slotReloadCoverArt()));


    // Disable editing
    //setEditTriggers(QAbstractItemView::NoEditTriggers);

    // Create all the context m_pMenu->actions (stuff that shows up when you
    //right-click)
    createActions();

    //Connect slots and signals to make the world go 'round.
    connect(this, SIGNAL(doubleClicked(const QModelIndex &)),
            this, SLOT(slotMouseDoubleClicked(const QModelIndex &)));

    connect(&m_playlistMapper, SIGNAL(mapped(int)),
            this, SLOT(addSelectionToPlaylist(int)));
    connect(&m_crateMapper, SIGNAL(mapped(int)),
            this, SLOT(addSelectionToCrate(int)));

    m_pCOTGuiTick = new ControlProxy("[Master]", "guiTick50ms", this);
    m_pCOTGuiTick->connectValueChanged(SLOT(slotGuiTick50ms(double)));

    connect(this, SIGNAL(scrollValueChanged(int)),
            this, SLOT(slotScrollValueChanged(int)));

    QShortcut *setFocusShortcut = new QShortcut(
        QKeySequence(tr("ESC", "Focus")), this);
    connect(setFocusShortcut, SIGNAL(activated()),
            this, SLOT(setFocus()));
}

WTrackTableView::~WTrackTableView() {
    WTrackTableViewHeader* pHeader =
            dynamic_cast<WTrackTableViewHeader*>(horizontalHeader());
    if (pHeader) {
        pHeader->saveHeaderState();
    }

    delete m_pImportMetadataFromFileAct;
    delete m_pImportMetadataFromMusicBrainzAct;
    delete m_pExportMetadataAct;
    delete m_pAddToPreviewDeck;
    delete m_pAutoDJBottomAct;
    delete m_pAutoDJTopAct;
    delete m_pAutoDJReplaceAct;
    delete m_pRemoveAct;
    delete m_pHideAct;
    delete m_pUnhideAct;
    delete m_pPropertiesAct;
    delete m_pMenu;
    delete m_pPlaylistMenu;
    delete m_pCoverMenu;
    delete m_pCrateMenu;
    delete m_pBpmLockAction;
    delete m_pBpmUnlockAction;
    delete m_pBpmDoubleAction;
    delete m_pBpmHalveAction;
    delete m_pBpmTwoThirdsAction;
    delete m_pBpmThreeFourthsAction;
    delete m_pBpmFourThirdsAction;
    delete m_pBpmThreeHalvesAction;
    delete m_pBPMMenu;
    delete m_pClearBeatsAction;
    delete m_pClearWaveformAction;
    delete m_pReplayGainResetAction;
    delete m_pPurgeAct;
    delete m_pFileBrowserAct;
    delete m_pResetPlayedAct;
    delete m_pSamplerMenu;
}

void WTrackTableView::enableCachedOnly() {
    if (!m_loadCachedOnly) {
        // don't try to load and search covers, drawing only
        // covers which are already in the QPixmapCache.
        emit(onlyCachedCoverArt(true));
        m_loadCachedOnly = true;
    }
    m_lastUserAction = mixxx::Time::elapsed();
}

void WTrackTableView::slotScrollValueChanged(int /*unused*/) {
    enableCachedOnly();
}

void WTrackTableView::selectionChanged(const QItemSelection& selected,
                                       const QItemSelection& deselected) {
    m_selectionChangedSinceLastGuiTick = true;
    enableCachedOnly();
    QTableView::selectionChanged(selected, deselected);
}

void WTrackTableView::slotGuiTick50ms(double /*unused*/) {
    // if the user is stopped in the same row for more than 0.1 s,
    // we load un-cached cover arts as well.
    mixxx::Duration timeDelta = mixxx::Time::elapsed() - m_lastUserAction;
    if (m_loadCachedOnly && timeDelta > mixxx::Duration::fromMillis(100)) {

        // Show the currently selected track in the large cover art view. Doing
        // this in selectionChanged slows down scrolling performance so we wait
        // until the user has stopped interacting first.
        if (m_selectionChangedSinceLastGuiTick) {
            const QModelIndexList indices = selectionModel()->selectedRows();
            if (indices.size() > 0 && indices.last().isValid()) {
                TrackModel* trackModel = getTrackModel();
                if (trackModel) {
                    TrackPointer pTrack = trackModel->getTrack(indices.last());
                    if (pTrack) {
                        emit(trackSelected(pTrack));
                    }
                }
            } else {
                emit(trackSelected(TrackPointer()));
            }
            m_selectionChangedSinceLastGuiTick = false;
        }

        // This allows CoverArtDelegate to request that we load covers from disk
        // (as opposed to only serving them from cache).
        emit(onlyCachedCoverArt(false));
        m_loadCachedOnly = false;
    }
}

// slot
void WTrackTableView::loadTrackModel(QAbstractItemModel *model) {
    //qDebug() << "WTrackTableView::loadTrackModel()" << model;

    TrackModel* trackModel = dynamic_cast<TrackModel*>(model);

    VERIFY_OR_DEBUG_ASSERT(model) {
        return;
    }
    VERIFY_OR_DEBUG_ASSERT(trackModel) {
        return;
    }

    /* If the model has not changed
     * there's no need to exchange the headers
     * this will cause a small GUI freeze
     */
    if (getTrackModel() == trackModel) {
        // Re-sort the table even if the track model is the same. This triggers
        // a select() if the table is dirty.
        doSortByColumn(horizontalHeader()->sortIndicatorSection());
        return;
    }

    // The "coverLocation" and "hash" column numbers are required very often
    // by slotLoadCoverArt(). As this value will not change when the model
    // still the same, we must avoid doing hundreds of "fieldIndex" calls
    // when it is completely unnecessary...
    m_iCoverSourceColumn = trackModel->fieldIndex(LIBRARYTABLE_COVERART_SOURCE);
    m_iCoverTypeColumn = trackModel->fieldIndex(LIBRARYTABLE_COVERART_TYPE);
    m_iCoverLocationColumn = trackModel->fieldIndex(LIBRARYTABLE_COVERART_LOCATION);
    m_iCoverHashColumn = trackModel->fieldIndex(LIBRARYTABLE_COVERART_HASH);
    m_iCoverColumn = trackModel->fieldIndex(LIBRARYTABLE_COVERART);
    m_iTrackLocationColumn = trackModel->fieldIndex(TRACKLOCATIONSTABLE_LOCATION);

    setVisible(false);

    // Save the previous track model's header state
    WTrackTableViewHeader* oldHeader =
            dynamic_cast<WTrackTableViewHeader*>(horizontalHeader());
    if (oldHeader) {
        oldHeader->saveHeaderState();
    }

    // rryan 12/2009 : Due to a bug in Qt, in order to switch to a model with
    // different columns than the old model, we have to create a new horizontal
    // header. Also, for some reason the WTrackTableView has to be hidden or
    // else problems occur. Since we parent the WtrackTableViewHeader's to the
    // WTrackTableView, they are automatically deleted.
    auto header = new WTrackTableViewHeader(Qt::Horizontal, this);

    // WTF(rryan) The following saves on unnecessary work on the part of
    // WTrackTableHeaderView. setHorizontalHeader() calls setModel() on the
    // current horizontal header. If this happens on the old
    // WTrackTableViewHeader, then it will save its old state, AND do the work
    // of initializing its menus on the new model. We create a new
    // WTrackTableViewHeader, so this is wasteful. Setting a temporary
    // QHeaderView here saves on setModel() calls. Since we parent the
    // QHeaderView to the WTrackTableView, it is automatically deleted.
    auto tempHeader = new QHeaderView(Qt::Horizontal, this);
    /* Tobias Rafreider: DO NOT SET SORTING TO TRUE during header replacement
     * Otherwise, setSortingEnabled(1) will immediately trigger sortByColumn()
     * For some reason this will cause 4 select statements in series
     * from which 3 are redundant --> expensive at all
     *
     * Sorting columns, however, is possible because we
     * enable clickable sorting indicators some lines below.
     * Furthermore, we connect signal 'sortIndicatorChanged'.
     *
     * Fixes Bug #672762
     */

    setSortingEnabled(false);
    setHorizontalHeader(tempHeader);

    setModel(model);
    setHorizontalHeader(header);
    header->setMovable(true);
    header->setClickable(true);
    header->setHighlightSections(true);
    header->setSortIndicatorShown(m_sorting);
    header->setDefaultAlignment(Qt::AlignLeft);

    // Initialize all column-specific things
    for (int i = 0; i < model->columnCount(); ++i) {
        // Setup delegates according to what the model tells us
        QAbstractItemDelegate* delegate = trackModel->delegateForColumn(i, this);
        // We need to delete the old delegates, since the docs say the view will
        // not take ownership of them.
        QAbstractItemDelegate* old_delegate = itemDelegateForColumn(i);
        // If delegate is NULL, it will unset the delegate for the column
        setItemDelegateForColumn(i, delegate);
        delete old_delegate;

        // Show or hide the column based on whether it should be shown or not.
        if (trackModel->isColumnInternal(i)) {
            //qDebug() << "Hiding column" << i;
            horizontalHeader()->hideSection(i);
        }
        /* If Mixxx starts the first time or the header states have been cleared
         * due to database schema evolution we gonna hide all columns that may
         * contain a potential large number of NULL values.  This will hide the
         * key colum by default unless the user brings it to front
         */
        if (trackModel->isColumnHiddenByDefault(i) &&
            !header->hasPersistedHeaderState()) {
            //qDebug() << "Hiding column" << i;
            horizontalHeader()->hideSection(i);
        }
    }

    if (m_sorting) {
        // NOTE: Should be a UniqueConnection but that requires Qt 4.6
        connect(horizontalHeader(), SIGNAL(sortIndicatorChanged(int, Qt::SortOrder)),
                this, SLOT(doSortByColumn(int)), Qt::AutoConnection);

        // Stupid hack that assumes column 0 is never visible, but this is a weak
        // proxy for "there was a saved column sort order"
        if (horizontalHeader()->sortIndicatorSection() > 0) {
            // Sort by the saved sort section and order. This line sorts the
            // TrackModel and in turn generates a select()
            horizontalHeader()->setSortIndicator(horizontalHeader()->sortIndicatorSection(),
                                                 horizontalHeader()->sortIndicatorOrder());
        } else {
            // No saved order is present. Use the TrackModel's default sort order.
            int sortColumn = trackModel->defaultSortColumn();
            Qt::SortOrder sortOrder = trackModel->defaultSortOrder();

            // If the TrackModel has an invalid or internal column as its default
            // sort, find the first non-internal column and sort by that.
            while (sortColumn < 0 || trackModel->isColumnInternal(sortColumn)) {
                sortColumn++;
            }
            // This line sorts the TrackModel and in turn generates a select()
            horizontalHeader()->setSortIndicator(sortColumn, sortOrder);
        }
    }

    // Set up drag and drop behavior according to whether or not the track
    // model says it supports it.

    // Defaults
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    // Always enable drag for now (until we have a model that doesn't support
    // this.)
    setDragEnabled(true);

    if (modelHasCapabilities(TrackModel::TRACKMODELCAPS_RECEIVEDROPS)) {
        setDragDropMode(QAbstractItemView::DragDrop);
        setDropIndicatorShown(true);
        setAcceptDrops(true);
        //viewport()->setAcceptDrops(true);
    }

    // Possible giant fuckup alert - It looks like Qt has something like these
    // caps built-in, see http://doc.trolltech.com/4.5/qt.html#ItemFlag-enum and
    // the flags(...) function that we're already using in LibraryTableModel. I
    // haven't been able to get it to stop us from using a model as a drag
    // target though, so my hax above may not be completely unjustified.

    setVisible(true);
}

void WTrackTableView::createActions() {
    DEBUG_ASSERT(m_pMenu);
    DEBUG_ASSERT(m_pSamplerMenu);

    m_pRemoveAct = new QAction(tr("Remove"), this);
    connect(m_pRemoveAct, SIGNAL(triggered()), this, SLOT(slotRemove()));

    m_pHideAct = new QAction(tr("Hide from Library"), this);
    connect(m_pHideAct, SIGNAL(triggered()), this, SLOT(slotHide()));

    m_pUnhideAct = new QAction(tr("Unhide from Library"), this);
    connect(m_pUnhideAct, SIGNAL(triggered()), this, SLOT(slotUnhide()));

    m_pPurgeAct = new QAction(tr("Purge from Library"), this);
    connect(m_pPurgeAct, SIGNAL(triggered()), this, SLOT(slotPurge()));

    m_pPropertiesAct = new QAction(tr("Properties"), this);
    connect(m_pPropertiesAct, SIGNAL(triggered()),
            this, SLOT(slotShowTrackInfo()));

    m_pFileBrowserAct = new QAction(tr("Open in File Browser"), this);
    connect(m_pFileBrowserAct, SIGNAL(triggered()),
            this, SLOT(slotOpenInFileBrowser()));

    m_pAutoDJBottomAct = new QAction(tr("Add to Auto DJ Queue (bottom)"), this);
    connect(m_pAutoDJBottomAct, SIGNAL(triggered()),
            this, SLOT(slotSendToAutoDJBottom()));

    m_pAutoDJTopAct = new QAction(tr("Add to Auto DJ Queue (top)"), this);
    connect(m_pAutoDJTopAct, SIGNAL(triggered()),
            this, SLOT(slotSendToAutoDJTop()));

    m_pAutoDJReplaceAct = new QAction(tr("Add to Auto DJ Queue (replace)"), this);
    connect(m_pAutoDJReplaceAct, SIGNAL(triggered()),
            this, SLOT(slotSendToAutoDJReplace()));

    m_pImportMetadataFromFileAct = new QAction(tr("Import Metadata from File"), this);
    connect(m_pImportMetadataFromFileAct, SIGNAL(triggered()),
            this, SLOT(slotImportTrackMetadataFromFileTags()));

    m_pImportMetadataFromMusicBrainzAct = new QAction(tr("Import Metadata from MusicBrainz"),this);
    connect(m_pImportMetadataFromMusicBrainzAct, SIGNAL(triggered()),
            this, SLOT(slotShowDlgTagFetcher()));

    m_pExportMetadataAct = new QAction(tr("Export Metadata into File"), this);
    connect(m_pExportMetadataAct, SIGNAL(triggered()),
            this, SLOT(slotExportTrackMetadataIntoFileTags()));

    m_pAddToPreviewDeck = new QAction(tr("Load to Preview Deck"), this);
    // currently there is only one preview deck so just map it here.
    QString previewDeckGroup = PlayerManager::groupForPreviewDeck(0);
    m_deckMapper.setMapping(m_pAddToPreviewDeck, previewDeckGroup);
    connect(m_pAddToPreviewDeck, SIGNAL(triggered()),
            &m_deckMapper, SLOT(map()));

    m_pResetPlayedAct = new QAction(tr("Reset Play Count"), this);
    connect(m_pResetPlayedAct, SIGNAL(triggered()),
            this, SLOT(slotResetPlayed()));

    m_pBpmLockAction = new QAction(tr("Lock BPM"), this);
    m_pBpmUnlockAction = new QAction(tr("Unlock BPM"), this);
    connect(m_pBpmLockAction, SIGNAL(triggered()),
            this, SLOT(slotLockBpm()));
    connect(m_pBpmUnlockAction, SIGNAL(triggered()),
            this, SLOT(slotUnlockBpm()));

    //new BPM actions
    m_pBpmDoubleAction = new QAction(tr("Double BPM"), this);
    m_pBpmHalveAction = new QAction(tr("Halve BPM"), this);
    m_pBpmTwoThirdsAction = new QAction(tr("2/3 BPM"), this);
    m_pBpmThreeFourthsAction = new QAction(tr("3/4 BPM"), this);
    m_pBpmFourThirdsAction = new QAction(tr("4/3 BPM"), this);
    m_pBpmThreeHalvesAction = new QAction(tr("3/2 BPM"), this);

    m_BpmMapper.setMapping(m_pBpmDoubleAction, Beats::DOUBLE);
    m_BpmMapper.setMapping(m_pBpmHalveAction, Beats::HALVE);
    m_BpmMapper.setMapping(m_pBpmTwoThirdsAction, Beats::TWOTHIRDS);
    m_BpmMapper.setMapping(m_pBpmThreeFourthsAction, Beats::THREEFOURTHS);
    m_BpmMapper.setMapping(m_pBpmFourThirdsAction, Beats::FOURTHIRDS);
    m_BpmMapper.setMapping(m_pBpmThreeHalvesAction, Beats::THREEHALVES);

    connect(m_pBpmDoubleAction, SIGNAL(triggered()),
            &m_BpmMapper, SLOT(map()));
    connect(m_pBpmHalveAction, SIGNAL(triggered()),
            &m_BpmMapper, SLOT(map()));
    connect(m_pBpmTwoThirdsAction, SIGNAL(triggered()),
            &m_BpmMapper, SLOT(map()));
    connect(m_pBpmThreeFourthsAction, SIGNAL(triggered()),
            &m_BpmMapper, SLOT(map()));
    connect(m_pBpmFourThirdsAction, SIGNAL(triggered()),
            &m_BpmMapper, SLOT(map()));
    connect(m_pBpmThreeHalvesAction, SIGNAL(triggered()),
            &m_BpmMapper, SLOT(map()));

    m_pClearBeatsAction = new QAction(tr("Clear BPM and Beatgrid"), this);
    connect(m_pClearBeatsAction, SIGNAL(triggered()),
            this, SLOT(slotClearBeats()));

    m_pClearWaveformAction = new QAction(tr("Clear Waveform"), this);
    connect(m_pClearWaveformAction, SIGNAL(triggered()),
            this, SLOT(slotClearWaveform()));

    m_pReplayGainResetAction = new QAction(tr("Reset ReplayGain"), this);
    connect(m_pReplayGainResetAction, SIGNAL(triggered()),
            this, SLOT(slotReplayGainReset()));
}

// slot
void WTrackTableView::slotMouseDoubleClicked(const QModelIndex &index) {
    if (!modelHasCapabilities(TrackModel::TRACKMODELCAPS_LOADTODECK)) {
        return;
    }
    // Read the current TrackLoadAction settings
    int action = DlgPrefLibrary::LOAD_TRACK_DECK; // default action
    if (modelHasCapabilities(TrackModel::TRACKMODELCAPS_ADDTOAUTODJ)) {
        action = m_pConfig->getValueString(ConfigKey("[Library]","TrackLoadAction")).toInt();
    }
    switch (action) {
    case DlgPrefLibrary::ADD_TRACK_BOTTOM:
            sendToAutoDJ(PlaylistDAO::AutoDJSendLoc::BOTTOM); // add track to Auto-DJ Queue (bottom)
            break;
    case DlgPrefLibrary::ADD_TRACK_TOP:
            sendToAutoDJ(PlaylistDAO::AutoDJSendLoc::TOP); // add track to Auto-DJ Queue (top)
            break;
    default: // load track to next available deck
            TrackModel* trackModel = getTrackModel();
            TrackPointer pTrack;
            if (trackModel && (pTrack = trackModel->getTrack(index))) {
                emit(loadTrack(pTrack));
            }
            break;
    }
}

void WTrackTableView::loadSelectionToGroup(QString group, bool play) {
    QModelIndexList indices = selectionModel()->selectedRows();
    if (indices.size() > 0) {
        // If the track load override is disabled, check to see if a track is
        // playing before trying to load it
        if (!(m_pConfig->getValueString(
            ConfigKey("[Controls]","AllowTrackLoadToPlayingDeck")).toInt())) {
            // TODO(XXX): Check for other than just the first preview deck.
            if (group != "[PreviewDeck1]" &&
                    ControlObject::get(ConfigKey(group, "play")) > 0.0) {
                return;
            }
        }
        QModelIndex index = indices.at(0);
        TrackModel* trackModel = getTrackModel();
        TrackPointer pTrack;
        if (trackModel &&
                (pTrack = trackModel->getTrack(index))) {
            emit(loadTrackToPlayer(pTrack, group, play));
        }
    }
}

void WTrackTableView::slotRemove() {
    QModelIndexList indices = selectionModel()->selectedRows();
    if (indices.size() > 0) {
        TrackModel* trackModel = getTrackModel();
        if (trackModel) {
            trackModel->removeTracks(indices);
        }
    }
}

void WTrackTableView::slotPurge() {
    QModelIndexList indices = selectionModel()->selectedRows();
    if (indices.size() > 0) {
        TrackModel* trackModel = getTrackModel();
        if (trackModel) {
            trackModel->purgeTracks(indices);
        }
    }
}

void WTrackTableView::slotOpenInFileBrowser() {
    TrackModel* trackModel = getTrackModel();
    if (!trackModel) {
        return;
    }

    QModelIndexList indices = selectionModel()->selectedRows();

    QSet<QString> sDirs;
    for (const QModelIndex& index : indices) {
        if (!index.isValid()) {
            continue;
        }

        QDir dir;
        QStringList splittedPath = trackModel->getTrackLocation(index).split("/");
        do {
            dir = QDir(splittedPath.join("/"));
            splittedPath.removeLast();
        } while (!dir.exists() && splittedPath.size());

        // This function does not work for a non-existent directory!
        // so it is essential that in the worst case it try opening
        // a valid directory, in this case, 'QDir::home()'.
        // Otherwise nothing would happen...
        if (!dir.exists()) {
            // it ensures a valid dir for any OS (Windows)
            dir = QDir::home();
        }

        // not open the same dir twice
        QString dirPath = dir.absolutePath();
        if (!sDirs.contains(dirPath)) {
            sDirs.insert(dirPath);
            QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
        }
    }
}

void WTrackTableView::slotHide() {
    QModelIndexList indices = selectionModel()->selectedRows();
    if (indices.size() > 0) {
        TrackModel* trackModel = getTrackModel();
        if (trackModel) {
            trackModel->hideTracks(indices);
        }
    }
}

void WTrackTableView::slotUnhide() {
    QModelIndexList indices = selectionModel()->selectedRows();

    if (indices.size() > 0) {
        TrackModel* trackModel = getTrackModel();
        if (trackModel) {
            trackModel->unhideTracks(indices);
        }
    }
}

void WTrackTableView::slotTrackInfoClosed() {
    DlgTrackInfo* pTrackInfo = m_pTrackInfo.take();
    // We are in a slot directly invoked from DlgTrackInfo. Delete it
    // later.
    if (pTrackInfo != nullptr) {
        pTrackInfo->deleteLater();
    }
}

void WTrackTableView::slotTagFetcherClosed() {
    DlgTagFetcher* pTagFetcher = m_pTagFetcher.take();
    // We are in a slot directly invoked from DlgTagFetcher. Delete it
    // later.
    if (pTagFetcher != nullptr) {
        pTagFetcher->deleteLater();
    }
}

void WTrackTableView::slotShowTrackInfo() {
    QModelIndexList indices = selectionModel()->selectedRows();

    if (indices.size() > 0) {
        showTrackInfo(indices[0]);
    }
}

void WTrackTableView::slotNextTrackInfo() {
    QModelIndex nextRow = currentTrackInfoIndex.sibling(
        currentTrackInfoIndex.row()+1, currentTrackInfoIndex.column());
    if (nextRow.isValid()) {
        showTrackInfo(nextRow);
        if (!m_pTagFetcher.isNull()) {
            showDlgTagFetcher(nextRow);
        }
    }
}

void WTrackTableView::slotPrevTrackInfo() {
    QModelIndex prevRow = currentTrackInfoIndex.sibling(
        currentTrackInfoIndex.row()-1, currentTrackInfoIndex.column());
    if (prevRow.isValid()) {
        showTrackInfo(prevRow);
        if (!m_pTagFetcher.isNull()) {
            showDlgTagFetcher(prevRow);
        }
    }
}

void WTrackTableView::showTrackInfo(QModelIndex index) {
    TrackModel* trackModel = getTrackModel();

    if (!trackModel) {
        return;
    }

    if (m_pTrackInfo.isNull()) {
        // Give a NULL parent because otherwise it inherits our style which can
        // make it unreadable. Bug #673411
        m_pTrackInfo.reset(new DlgTrackInfo(nullptr));

        connect(m_pTrackInfo.data(), SIGNAL(next()),
                this, SLOT(slotNextTrackInfo()));
        connect(m_pTrackInfo.data(), SIGNAL(previous()),
                this, SLOT(slotPrevTrackInfo()));
        connect(m_pTrackInfo.data(), SIGNAL(showTagFetcher(TrackPointer)),
                this, SLOT(slotShowTrackInTagFetcher(TrackPointer)));
        connect(m_pTrackInfo.data(), SIGNAL(finished(int)),
                this, SLOT(slotTrackInfoClosed()));
    }
    TrackPointer pTrack = trackModel->getTrack(index);
    m_pTrackInfo->loadTrack(pTrack); // NULL is fine.
    currentTrackInfoIndex = index;
    m_pTrackInfo->show();
}

void WTrackTableView::slotNextDlgTagFetcher() {
    QModelIndex nextRow = currentTrackInfoIndex.sibling(
        currentTrackInfoIndex.row()+1, currentTrackInfoIndex.column());
    if (nextRow.isValid()) {
        showDlgTagFetcher(nextRow);
        if (!m_pTrackInfo.isNull()) {
            showTrackInfo(nextRow);
        }
    }
}

void WTrackTableView::slotPrevDlgTagFetcher() {
    QModelIndex prevRow = currentTrackInfoIndex.sibling(
        currentTrackInfoIndex.row()-1, currentTrackInfoIndex.column());
    if (prevRow.isValid()) {
        showDlgTagFetcher(prevRow);
        if (!m_pTrackInfo.isNull()) {
            showTrackInfo(prevRow);
        }
    }
}

void WTrackTableView::showDlgTagFetcher(QModelIndex index) {
    TrackModel* trackModel = getTrackModel();

    if (!trackModel) {
        return;
    }

    TrackPointer pTrack = trackModel->getTrack(index);
    currentTrackInfoIndex = index;
    slotShowTrackInTagFetcher(pTrack);
}

void WTrackTableView::slotShowTrackInTagFetcher(TrackPointer pTrack) {
    if (m_pTagFetcher.isNull()) {
        m_pTagFetcher.reset(new DlgTagFetcher(nullptr));
        connect(m_pTagFetcher.data(), SIGNAL(next()),
                this, SLOT(slotNextDlgTagFetcher()));
        connect(m_pTagFetcher.data(), SIGNAL(previous()),
                this, SLOT(slotPrevDlgTagFetcher()));
        connect(m_pTagFetcher.data(), SIGNAL(finished(int)),
                this, SLOT(slotTagFetcherClosed()));
    }

    // NULL is fine
    m_pTagFetcher->loadTrack(pTrack);
    m_pTagFetcher->show();
}

void WTrackTableView::slotShowDlgTagFetcher() {
    QModelIndexList indices = selectionModel()->selectedRows();

    if (indices.size() > 0) {
        showDlgTagFetcher(indices[0]);
    }
}

void WTrackTableView::contextMenuEvent(QContextMenuEvent* event) {
    QModelIndexList indices = selectionModel()->selectedRows();

    // Gray out some stuff if multiple songs were selected.
    bool oneSongSelected = indices.size() == 1;
    TrackModel* trackModel = getTrackModel();

    m_pMenu->clear();

    if (modelHasCapabilities(TrackModel::TRACKMODELCAPS_ADDTOAUTODJ)) {
        m_pMenu->addAction(m_pAutoDJBottomAct);
        m_pMenu->addAction(m_pAutoDJTopAct);
        m_pMenu->addAction(m_pAutoDJReplaceAct);
        m_pMenu->addSeparator();
    }

    if (modelHasCapabilities(TrackModel::TRACKMODELCAPS_LOADTODECK)) {
        int iNumDecks = m_pNumDecks->get();
        if (iNumDecks > 0) {
            for (int i = 1; i <= iNumDecks; ++i) {
                // PlayerManager::groupForDeck is 0-indexed.
                QString deckGroup = PlayerManager::groupForDeck(i - 1);
                bool deckPlaying = ControlObject::get(
                        ConfigKey(deckGroup, "play")) > 0.0;
                bool loadTrackIntoPlayingDeck = m_pConfig->getValue<bool>(
                        ConfigKey("[Controls]", "AllowTrackLoadToPlayingDeck"));
                bool deckEnabled = (!deckPlaying  || loadTrackIntoPlayingDeck)  && oneSongSelected;
                QAction* pAction = new QAction(tr("Load to Deck %1").arg(i), m_pMenu);
                pAction->setEnabled(deckEnabled);
                m_pMenu->addAction(pAction);
                m_deckMapper.setMapping(pAction, deckGroup);
                connect(pAction, SIGNAL(triggered()), &m_deckMapper, SLOT(map()));
            }
        }
    }

    if (modelHasCapabilities(TrackModel::TRACKMODELCAPS_LOADTOSAMPLER)) {
        int iNumSamplers = m_pNumSamplers->get();
        if (iNumSamplers > 0) {
            m_pSamplerMenu->clear();
            for (int i = 1; i <= iNumSamplers; ++i) {
                // PlayerManager::groupForSampler is 0-indexed.
                QString samplerGroup = PlayerManager::groupForSampler(i - 1);
                bool samplerPlaying = ControlObject::get(
                        ConfigKey(samplerGroup, "play")) > 0.0;
                bool samplerEnabled = !samplerPlaying && oneSongSelected;
                QAction* pAction = new QAction(tr("Sampler %1").arg(i), m_pSamplerMenu);
                pAction->setEnabled(samplerEnabled);
                m_pSamplerMenu->addAction(pAction);
                m_samplerMapper.setMapping(pAction, samplerGroup);
                connect(pAction, SIGNAL(triggered()), &m_samplerMapper, SLOT(map()));
            }
            m_pMenu->addMenu(m_pSamplerMenu);
        }
    }

    if (modelHasCapabilities(TrackModel::TRACKMODELCAPS_LOADTOPREVIEWDECK) &&
        m_pNumPreviewDecks->get() > 0.0) {
        m_pMenu->addAction(m_pAddToPreviewDeck);
    }

    m_pMenu->addSeparator();

    if (modelHasCapabilities(TrackModel::TRACKMODELCAPS_ADDTOPLAYLIST)) {
        m_pPlaylistMenu->clear();
        PlaylistDAO& playlistDao = m_pTrackCollection->getPlaylistDAO();
        QMap<QString,int> playlists;
        int numPlaylists = playlistDao.playlistCount();
        for (int i = 0; i < numPlaylists; ++i) {
            int iPlaylistId = playlistDao.getPlaylistId(i);
            playlists.insert(playlistDao.getPlaylistName(iPlaylistId), iPlaylistId);
        }
        QMapIterator<QString, int> it(playlists);
        while (it.hasNext()) {
            it.next();
            if (!playlistDao.isHidden(it.value())) {
                // No leak because making the menu the parent means they will be
                // auto-deleted
                auto pAction = new QAction(it.key(), m_pPlaylistMenu);
                bool locked = playlistDao.isPlaylistLocked(it.value());
                pAction->setEnabled(!locked);
                m_pPlaylistMenu->addAction(pAction);
                m_playlistMapper.setMapping(pAction, it.value());
                connect(pAction, SIGNAL(triggered()), &m_playlistMapper, SLOT(map()));
            }
        }
        m_pPlaylistMenu->addSeparator();
        QAction* newPlaylistAction = new QAction(tr("Create New Playlist"), m_pPlaylistMenu);
        m_pPlaylistMenu->addAction(newPlaylistAction);
        m_playlistMapper.setMapping(newPlaylistAction, -1);// -1 to signify new playlist
        connect(newPlaylistAction, SIGNAL(triggered()), &m_playlistMapper, SLOT(map()));

        m_pMenu->addMenu(m_pPlaylistMenu);
    }

    if (modelHasCapabilities(TrackModel::TRACKMODELCAPS_ADDTOCRATE)) {
        m_pCrateMenu->clear();
        CrateSelectResult allCrates(m_pTrackCollection->crates().selectCrates());
        Crate crate;
        while (allCrates.populateNext(&crate)) {
            auto pAction = std::make_unique<QAction>(crate.getName(), m_pCrateMenu);
            pAction->setEnabled(!crate.isLocked());
            m_crateMapper.setMapping(pAction.get(), crate.getId().toInt());
            connect(pAction.get(), SIGNAL(triggered()), &m_crateMapper, SLOT(map()));
            m_pCrateMenu->addAction(pAction.get());
            pAction.release();
        }
        m_pCrateMenu->addSeparator();
        QAction* newCrateAction = new QAction(tr("Create New Crate"), m_pCrateMenu);
        m_pCrateMenu->addAction(newCrateAction);
        m_crateMapper.setMapping(newCrateAction, CrateId().toInt());// invalid crate id for new crate
        connect(newCrateAction, SIGNAL(triggered()), &m_crateMapper, SLOT(map()));

        m_pMenu->addMenu(m_pCrateMenu);
    }
    m_pMenu->addSeparator();

    //start of BPM section of menu
    if (modelHasCapabilities(TrackModel::TRACKMODELCAPS_MANIPULATEBEATS)) {
        m_pBPMMenu->addAction(m_pBpmDoubleAction);
        m_pBPMMenu->addAction(m_pBpmHalveAction);
        m_pBPMMenu->addAction(m_pBpmTwoThirdsAction);
        m_pBPMMenu->addAction(m_pBpmThreeFourthsAction);
        m_pBPMMenu->addAction(m_pBpmFourThirdsAction);
        m_pBPMMenu->addAction(m_pBpmThreeHalvesAction);
        m_pBPMMenu->addSeparator();
        m_pBPMMenu->addAction(m_pBpmLockAction);
        m_pBPMMenu->addAction(m_pBpmUnlockAction);
        m_pBPMMenu->addSeparator();
        if (oneSongSelected) {
            if (trackModel == nullptr) {
                return;
            }
            int column = trackModel->fieldIndex("bpm_lock");
            QModelIndex index = indices.at(0).sibling(indices.at(0).row(),column);
            if (index.data().toBool()) { //BPM is locked
                m_pBpmUnlockAction->setEnabled(true);
                m_pBpmLockAction->setEnabled(false);
                m_pBpmDoubleAction->setEnabled(false);
                m_pBpmHalveAction->setEnabled(false);
                m_pBpmTwoThirdsAction->setEnabled(false);
                m_pBpmThreeFourthsAction->setEnabled(false);
                m_pBpmFourThirdsAction->setEnabled(false);
                m_pBpmThreeHalvesAction->setEnabled(false);
            } else { //BPM is not locked
                m_pBpmUnlockAction->setEnabled(false);
                m_pBpmLockAction->setEnabled(true);
                m_pBpmDoubleAction->setEnabled(true);
                m_pBpmHalveAction->setEnabled(true);
                m_pBpmTwoThirdsAction->setEnabled(true);
                m_pBpmThreeFourthsAction->setEnabled(true);
                m_pBpmFourThirdsAction->setEnabled(true);
                m_pBpmThreeHalvesAction->setEnabled(true);
            }
        } else {
            bool anyLocked = false; //true if any of the selected items are locked
            int column = trackModel->fieldIndex("bpm_lock");
            for (int i = 0; i < indices.size() && !anyLocked; ++i) {
                int row = indices.at(i).row();
                QModelIndex index = indices.at(i).sibling(row,column);
                if (index.data().toBool()) {
                    anyLocked = true;
                }
            }
            if (anyLocked) {
                m_pBpmLockAction->setEnabled(false);
                m_pBpmUnlockAction->setEnabled(true);
                m_pBpmDoubleAction->setEnabled(false);
                m_pBpmHalveAction->setEnabled(false);
                m_pBpmTwoThirdsAction->setEnabled(false);
                m_pBpmThreeFourthsAction->setEnabled(false);
                m_pBpmFourThirdsAction->setEnabled(false);
                m_pBpmThreeHalvesAction->setEnabled(false);
            } else {
                m_pBpmLockAction->setEnabled(true);
                m_pBpmUnlockAction->setEnabled(false);
                m_pBpmDoubleAction->setEnabled(true);
                m_pBpmHalveAction->setEnabled(true);
                m_pBpmTwoThirdsAction->setEnabled(true);
                m_pBpmThreeFourthsAction->setEnabled(true);
                m_pBpmFourThirdsAction->setEnabled(true);
                m_pBpmThreeHalvesAction->setEnabled(true);
            }
        }
    }
    //add BPM menu to pMenu
    m_pMenu->addMenu(m_pBPMMenu);
    //end of BPM section of menu

    if (modelHasCapabilities(TrackModel::TRACKMODELCAPS_CLEAR_BEATS)) {
        if (trackModel == nullptr) {
            return;
        }
        bool allowClear = true;
        int column = trackModel->fieldIndex("bpm_lock");
        for (int i = 0; i < indices.size() && allowClear; ++i) {
            int row = indices.at(i).row();
            QModelIndex index = indices.at(i).sibling(row,column);
            if (index.data().toBool()) {
                allowClear = false;
            }
        }
        m_pClearBeatsAction->setEnabled(allowClear);
        m_pBPMMenu->addAction(m_pClearBeatsAction);
    }

    m_pMenu->addAction(m_pClearWaveformAction);

    m_pMenu->addAction(m_pReplayGainResetAction);

    m_pMenu->addSeparator();
    if (modelHasCapabilities(TrackModel::TRACKMODELCAPS_IMPORTMETADATA)) {
        m_pMenu->addAction(m_pImportMetadataFromFileAct);
        m_pImportMetadataFromMusicBrainzAct->setEnabled(oneSongSelected);
        m_pMenu->addAction(m_pImportMetadataFromMusicBrainzAct);
        m_pMenu->addAction(m_pExportMetadataAct);
    }

    // Cover art menu only applies if at least one track is selected.
    if (indices.size()) {
        // We load a single track to get the necessary context for the cover (we use
        // last to be consistent with selectionChanged above).
        QModelIndex last = indices.last();
        CoverInfo info;
        info.source = static_cast<CoverInfo::Source>(
            last.sibling(last.row(), m_iCoverSourceColumn).data().toInt());
        info.type = static_cast<CoverInfo::Type>(
            last.sibling(last.row(), m_iCoverTypeColumn).data().toInt());
        info.hash = last.sibling(last.row(), m_iCoverHashColumn).data().toUInt();
        info.trackLocation = last.sibling(
            last.row(), m_iTrackLocationColumn).data().toString();
        info.coverLocation = last.sibling(
            last.row(), m_iCoverLocationColumn).data().toString();
        m_pCoverMenu->setCoverArt(info);
        m_pMenu->addMenu(m_pCoverMenu);
    }

    // REMOVE and HIDE should not be at the first menu position to avoid accidental clicks
    m_pMenu->addSeparator();
    bool locked = modelHasCapabilities(TrackModel::TRACKMODELCAPS_LOCKED);
    if (modelHasCapabilities(TrackModel::TRACKMODELCAPS_REMOVE)) {
        m_pRemoveAct->setEnabled(!locked);
        m_pMenu->addAction(m_pRemoveAct);
    }
    if (modelHasCapabilities(TrackModel::TRACKMODELCAPS_HIDE)) {
        m_pHideAct->setEnabled(!locked);
        m_pMenu->addAction(m_pHideAct);
    }
    if (modelHasCapabilities(TrackModel::TRACKMODELCAPS_UNHIDE)) {
        m_pUnhideAct->setEnabled(!locked);
        m_pMenu->addAction(m_pUnhideAct);
    }
    if (modelHasCapabilities(TrackModel::TRACKMODELCAPS_PURGE)) {
        m_pPurgeAct->setEnabled(!locked);
        m_pMenu->addAction(m_pPurgeAct);
    }
    if (modelHasCapabilities(TrackModel::TRACKMODELCAPS_RESETPLAYED)) {
        m_pMenu->addAction(m_pResetPlayedAct);
    }
    m_pMenu->addAction(m_pFileBrowserAct);
    m_pMenu->addSeparator();
    m_pPropertiesAct->setEnabled(oneSongSelected);
    m_pMenu->addAction(m_pPropertiesAct);

    //Create the right-click menu
    m_pMenu->popup(event->globalPos());
}

void WTrackTableView::onSearch(const QString& text) {
    TrackModel* trackModel = getTrackModel();
    if (trackModel) {
        trackModel->search(text);
    }
}

void WTrackTableView::onSearchStarting() {
    saveVScrollBarPos();
}

void WTrackTableView::onSearchCleared() {
    restoreVScrollBarPos();
    TrackModel* trackModel = getTrackModel();
    if (trackModel) {
        trackModel->search("");
    }
}

void WTrackTableView::onShow() {
}

void WTrackTableView::mouseMoveEvent(QMouseEvent* pEvent) {
    // Only use this for drag and drop if the LeftButton is pressed we need to
    // check for this because mousetracking is activated and this function is
    // called everytime the mouse is moved -- kain88 May 2012
    if (pEvent->buttons() != Qt::LeftButton) {
        // Needed for mouse-tracking to fire entered() events. If we call this
        // outside of this if statement then we get 'ghost' drags. See Bug
        // #1008737
        WLibraryTableView::mouseMoveEvent(pEvent);
        return;
    }

    TrackModel* trackModel = getTrackModel();
    if (!trackModel) {
        return;
    }
    //qDebug() << "MouseMoveEvent";
    // Iterate over selected rows and append each item's location url to a list.
    QList<QString> locations;
    QModelIndexList indices = selectionModel()->selectedRows();

    for (const QModelIndex& index : indices) {
        if (!index.isValid()) {
            continue;
        }
        locations.append(trackModel->getTrackLocation(index));
    }
    DragAndDropHelper::dragTrackLocations(locations, this, "library");
}

// Drag enter event, happens when a dragged item hovers over the track table view
void WTrackTableView::dragEnterEvent(QDragEnterEvent * event) {
    //qDebug() << "dragEnterEvent" << event->mimeData()->formats();
    if (event->mimeData()->hasUrls()) {
        if (event->source() == this) {
            if (modelHasCapabilities(TrackModel::TRACKMODELCAPS_REORDER)) {
                event->acceptProposedAction();
                return;
            }
        } else if (DragAndDropHelper::dragEnterAccept(*event->mimeData(),
                                                      "library", true, true)) {
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

// Drag move event, happens when a dragged item hovers over the track table view...
// It changes the drop handle to a "+" when the drag content is acceptable.
// Without it, the following drop is ignored.
void WTrackTableView::dragMoveEvent(QDragMoveEvent * event) {
    // Needed to allow auto-scrolling
    WLibraryTableView::dragMoveEvent(event);

    //qDebug() << "dragMoveEvent" << event->mimeData()->formats();
    if (event->mimeData()->hasUrls())
    {
        if (event->source() == this) {
            if (modelHasCapabilities(TrackModel::TRACKMODELCAPS_REORDER)) {
                event->acceptProposedAction();
            } else {
                event->ignore();
            }
        } else {
            event->acceptProposedAction();
        }
    } else {
        event->ignore();
    }
}

// Drag-and-drop "drop" event. Occurs when something is dropped onto the track table view
void WTrackTableView::dropEvent(QDropEvent * event) {
    TrackModel* trackModel = getTrackModel();

    // We only do things to the TrackModel in this method so if we don't have
    // one we should just bail.
    if (!trackModel) {
        return;
    }

    if (!event->mimeData()->hasUrls() || trackModel->isLocked()) {
        event->ignore();
        return;
    }

    // Save the vertical scrollbar position. Adding new tracks and moving tracks in
    // the SQL data models causes a select() (ie. generation of a new result set),
    // which causes view to reset itself. A view reset causes the widget to scroll back
    // up to the top, which is confusing when you're dragging and dropping. :)
    saveVScrollBarPos();


    // Calculate the model index where the track or tracks are destined to go.
    // (the "drop" position in a drag-and-drop)
    // The user usually drops on the seam between two rows.
    // We take the row below the seam for reference.
    int dropRow = rowAt(event->pos().y());
    int hight = rowHeight(dropRow);
    QPoint pointOfRowBelowSeam(event->pos().x(), event->pos().y() + hight / 2);
    QModelIndex destIndex = indexAt(pointOfRowBelowSeam);

    //qDebug() << "destIndex.row() is" << destIndex.row();

    // Drag and drop within this widget (track reordering)
    if (event->source() == this && modelHasCapabilities(TrackModel::TRACKMODELCAPS_REORDER)) {
        // Note the above code hides an ambiguous case when a
        // playlist is empty. For that reason, we can't factor that
        // code out to be common for both internal reordering
        // and external drag-and-drop. With internal reordering,
        // you can't have an empty playlist. :)

        //qDebug() << "track reordering" << __FILE__ << __LINE__;

        // Save a list of row (just plain ints) so we don't get screwed over
        // when the QModelIndexes all become invalid (eg. after moveTrack()
        // or addTrack())
        QModelIndexList indices = selectionModel()->selectedRows();

        QList<int> selectedRows;
        for (const QModelIndex& idx : indices) {
            selectedRows.append(idx.row());
        }

        // Note: The biggest subtlety in the way I've done this track reordering code
        // is that as soon as we've moved ANY track, all of our QModelIndexes probably
        // get screwed up. The starting point for the logic below is to say screw it to
        // the QModelIndexes, and just keep a list of row numbers to work from. That
        // ends up making the logic simpler and the behavior totally predictable,
        // which lets us do nice things like "restore" the selection model.

        // The model indices are sorted so that we remove the tracks from the table
        // in ascending order. This is necessary because if track A is above track B in
        // the table, and you remove track A, the model index for track B will change.
        // Sorting the indices first means we don't have to worry about this.
        //qSort(m_selectedIndices);
        //qSort(m_selectedIndices.begin(), m_selectedIndices.end(), qGreater<QModelIndex>());
        qSort(selectedRows);
        int maxRow = 0;
        int minRow = 0;
        if (!selectedRows.isEmpty()) {
            maxRow = selectedRows.last();
            minRow = selectedRows.first();
        }

        // Destination row, if destIndex is invalid we set it to last row + 1
        int destRow = destIndex.row() < 0 ? model()->rowCount() : destIndex.row();

        int selectedRowCount = selectedRows.count();
        int selectionRestoreStartRow = destRow;

        // Adjust first row of new selection
        if (destRow >= minRow && destRow <= maxRow) {
            // If you drag a contiguous selection of multiple tracks and drop
            // them somewhere inside that same selection, do nothing.
            return;
        } else {
            if (destRow < minRow) {
                // If we're moving the tracks _up_,
                // then reverse the order of the row selection
                // to make the algorithm below work as it is
                qSort(selectedRows.begin(),
                      selectedRows.end(),
                      qGreater<int>());
            } else {
               if (destRow > maxRow) {
                   // If we're moving the tracks _down_,
                   // adjust the first row to reselect
                   selectionRestoreStartRow =
                        selectionRestoreStartRow - selectedRowCount;
                }
            }
        }

        // For each row that needs to be moved...
        while (!selectedRows.isEmpty()) {
            int movedRow = selectedRows.takeFirst(); // Remember it's row index
            // Move it
            trackModel->moveTrack(model()->index(movedRow, 0), destIndex);

            // Move the row indices for rows that got bumped up
            // into the void we left, or down because of the new spot
            // we're taking.
            for (int i = 0; i < selectedRows.count(); i++) {
                if ((selectedRows[i] > movedRow) && (
                    (destRow > selectedRows[i]) )) {
                    selectedRows[i] = selectedRows[i] - 1;
                } else if ((selectedRows[i] < movedRow) &&
                            (destRow < selectedRows[i])) {
                    selectedRows[i] = selectedRows[i] + 1;
                }
            }
        }


        // Highlight the moved rows again (restoring the selection)
        //QModelIndex newSelectedIndex = destIndex;
        for (int i = 0; i < selectedRowCount; i++) {
            this->selectionModel()->select(model()->index(selectionRestoreStartRow + i, 0),
                                            QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    } else { // Drag and drop inside Mixxx is only for few rows, bulks happen here
        // Reset the selected tracks (if you had any tracks highlighted, it
        // clears them)
        this->selectionModel()->clear();

        // Add all the dropped URLs/tracks to the track model (playlist/crate)
        QList<QFileInfo> fileList = DragAndDropHelper::supportedTracksFromUrls(
            event->mimeData()->urls(), false, true);

        QList<QString> fileLocationList;
        for (const QFileInfo& fileInfo : fileList) {
            fileLocationList.append(TrackRef::location(fileInfo));
        }

        // Drag-and-drop from an external application
        // eg. dragging a track from Windows Explorer onto the track table.
        int numNewRows = fileLocationList.count();

        // Have to do this here because the index is invalid after
        // addTrack
        int selectionStartRow = destIndex.row();

        // Make a new selection starting from where the first track was
        // dropped, and select all the dropped tracks

        // If the track was dropped into an empty playlist, start at row
        // 0 not -1 :)
        if ((destIndex.row() == -1) && (model()->rowCount() == 0)) {
            selectionStartRow = 0;
        } else if ((destIndex.row() == -1) && (model()->rowCount() > 0)) {
            // If the track was dropped beyond the end of a playlist, then
            // we need to fudge the destination a bit...
            //qDebug() << "Beyond end of playlist";
            //qDebug() << "rowcount is:" << model()->rowCount();
            selectionStartRow = model()->rowCount();
        }

        // calling the addTracks returns number of failed additions
        int tracksAdded = trackModel->addTracks(destIndex, fileLocationList);

        // Decrement # of rows to select if some were skipped
        numNewRows -= (fileLocationList.size() - tracksAdded);

        // Create the selection, but only if the track model supports
        // reordering. (eg. crates don't support reordering/indexes)
        if (modelHasCapabilities(TrackModel::TRACKMODELCAPS_REORDER)) {
            for (int i = selectionStartRow; i < selectionStartRow + numNewRows; i++) {
                this->selectionModel()->select(model()->index(i, 0),
                                               QItemSelectionModel::Select |
                                               QItemSelectionModel::Rows);
            }
        }
    }

    event->acceptProposedAction();
    restoreVScrollBarPos();
}

TrackModel* WTrackTableView::getTrackModel() const {
    TrackModel* trackModel = dynamic_cast<TrackModel*>(model());
    return trackModel;
}

bool WTrackTableView::modelHasCapabilities(TrackModel::CapabilitiesFlags capabilities) const {
    TrackModel* trackModel = getTrackModel();
    return trackModel &&
            (trackModel->getCapabilities() & capabilities) == capabilities;
}

void WTrackTableView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return) {
        // It is not a good idea if 'key_return'
        // causes a track to load since we allow in-line editing
        // of table items in general
        return;
    } else {
        QTableView::keyPressEvent(event);
    }
}

void WTrackTableView::loadSelectedTrack() {
    QModelIndexList indexes = selectionModel()->selectedRows();
    if (indexes.size() > 0) {
        slotMouseDoubleClicked(indexes.at(0));
    }
}

void WTrackTableView::loadSelectedTrackToGroup(QString group, bool play) {
    loadSelectionToGroup(group, play);
}

void WTrackTableView::slotSendToAutoDJBottom() {
    // append to auto DJ
    sendToAutoDJ(PlaylistDAO::AutoDJSendLoc::BOTTOM);
}

void WTrackTableView::slotSendToAutoDJTop() {
    sendToAutoDJ(PlaylistDAO::AutoDJSendLoc::TOP);
}

void WTrackTableView::slotSendToAutoDJReplace() {
    sendToAutoDJ(PlaylistDAO::AutoDJSendLoc::REPLACE);
}

QList<TrackId> WTrackTableView::getSelectedTrackIds() const {
    QList<TrackId> trackIds;

    QItemSelectionModel* pSelectionModel = selectionModel();
    VERIFY_OR_DEBUG_ASSERT(pSelectionModel != nullptr) {
        qWarning() << "No selected tracks available";
        return trackIds;
    }

    TrackModel* pTrackModel = getTrackModel();
    VERIFY_OR_DEBUG_ASSERT(pTrackModel != nullptr) {
        qWarning() << "No selected tracks available";
        return trackIds;
    }

    const QModelIndexList rows = selectionModel()->selectedRows();
    trackIds.reserve(rows.size());
    for (const QModelIndex& row: rows) {
        const TrackId trackId = pTrackModel->getTrackId(row);
        VERIFY_OR_DEBUG_ASSERT(trackId.isValid()) {
            qWarning() << "Skipping invalid track id @" << row;
            continue;
        }
        trackIds.append(trackId);
    }

    return trackIds;
}

void WTrackTableView::sendToAutoDJ(PlaylistDAO::AutoDJSendLoc loc) {
    if (!modelHasCapabilities(TrackModel::TRACKMODELCAPS_ADDTOAUTODJ)) {
        return;
    }

    const QList<TrackId> trackIds = getSelectedTrackIds();
    if (trackIds.isEmpty()) {
        qWarning() << "No tracks selected for AutoDJ";
        return;
    }

    PlaylistDAO& playlistDao = m_pTrackCollection->getPlaylistDAO();

    // TODO(XXX): Care whether the append succeeded.
    m_pTrackCollection->unhideTracks(trackIds);
    playlistDao.sendToAutoDJ(trackIds, loc);
}

void WTrackTableView::slotImportTrackMetadataFromFileTags() {
    if (!modelHasCapabilities(TrackModel::TRACKMODELCAPS_IMPORTMETADATA)) {
        return;
    }

    QModelIndexList indices = selectionModel()->selectedRows();

    TrackModel* trackModel = getTrackModel();

    if (trackModel == nullptr) {
        return;
    }

    for (const QModelIndex& index : indices) {
        TrackPointer pTrack = trackModel->getTrack(index);
        if (pTrack && pTrack->isDirty()) {
            if (QMessageBox::Apply == QMessageBox::question(
                    nullptr,
                    tr("Import Track Metadata"),
                    tr("One or more selected tracks have unsaved changes. Continue anyway?"),
                    QMessageBox::Apply | QMessageBox::Cancel,
                    QMessageBox::Apply)) {
                // Continue with import
                break;
            } else {
                // Abort import
                return;
            }
        }
    }

    for (const QModelIndex& index : indices) {
        TrackPointer pTrack = trackModel->getTrack(index);
        if (pTrack) {
            // The user has explicitly requested to reload metadata from the file
            // to override the information within Mixxx! Custom cover art must be
            // reloaded separately.
            SoundSourceProxy(pTrack).updateTrackFromSource(
                    SoundSourceProxy::ImportTrackMetadataMode::Again);
        }
    }
}

void WTrackTableView::slotExportTrackMetadataIntoFileTags() {
    if (!modelHasCapabilities(TrackModel::TRACKMODELCAPS_IMPORTMETADATA)) {
        return;
    }

    QModelIndexList indices = selectionModel()->selectedRows();

    TrackModel* trackModel = getTrackModel();

    if (trackModel == NULL) {
        return;
    }

    if (QMessageBox::Apply != QMessageBox::question(
            nullptr,
            tr("Export Track Metadata"),
            tr("Write track metadata back into the underlying audio file(s)?\n\n"
                    "File modifications are deferred until considered safe and changes may not appear instantly!"
                    " Close Mixxx if you need to ensure that all pending write operations are finished."),
            QMessageBox::Apply | QMessageBox::Cancel,
            QMessageBox::Apply)) {
        return;
    }

    foreach (QModelIndex index, indices) {
        TrackPointer pTrack = trackModel->getTrack(index);
        if (pTrack) {
            // Export of metadata is deferred until all references to the
            // corresponding track object have been dropped. Otherwise
            // writing to files that are still used for playback might
            // cause crashes or at least audible glitches!
            pTrack->markForMetadataExport();
        }
    }
}

//slot for reset played count, sets count to 0 of one or more tracks
void WTrackTableView::slotResetPlayed() {
    QModelIndexList indices = selectionModel()->selectedRows();
    TrackModel* trackModel = getTrackModel();

    if (trackModel == nullptr) {
        return;
    }

    for (const QModelIndex& index : indices) {
        TrackPointer pTrack = trackModel->getTrack(index);
        if (pTrack) {
            pTrack->resetPlayCounter();
        }
    }
}

void WTrackTableView::addSelectionToPlaylist(int iPlaylistId) {
    const QList<TrackId> trackIds = getSelectedTrackIds();
    if (trackIds.isEmpty()) {
        qWarning() << "No tracks selected for playlist";
        return;
    }

    PlaylistDAO& playlistDao = m_pTrackCollection->getPlaylistDAO();

    if (iPlaylistId == -1) { // i.e. a new playlist is suppose to be created
        QString name;
        bool validNameGiven = false;

        do {
            bool ok = false;
            name = QInputDialog::getText(nullptr,
                    tr("Create New Playlist"),
                    tr("Enter name for new playlist:"),
                    QLineEdit::Normal,
                    tr("New Playlist"),
                    &ok).trimmed();
            if (!ok) {
                return;
            }
            if (playlistDao.getPlaylistIdFromName(name) != -1) {
                QMessageBox::warning(nullptr,
                        tr("Playlist Creation Failed"),
                        tr("A playlist by that name already exists."));
            } else if (name.isEmpty()) {
                QMessageBox::warning(nullptr,
                        tr("Playlist Creation Failed"),
                        tr("A playlist cannot have a blank name."));
            } else {
                validNameGiven = true;
            }
       } while (!validNameGiven);
       iPlaylistId = playlistDao.createPlaylist(name);//-1 is changed to the new playlist ID return from the DAO
       if (iPlaylistId == -1) {
           QMessageBox::warning(nullptr,
                                tr("Playlist Creation Failed"),
                                tr("An unknown error occurred while creating playlist: ")
                                 +name);
           return;
       }
    }

    // TODO(XXX): Care whether the append succeeded.
    m_pTrackCollection->unhideTracks(trackIds);
    playlistDao.appendTracksToPlaylist(trackIds, iPlaylistId);
}

void WTrackTableView::addSelectionToCrate(int iCrateId) {
    const QList<TrackId> trackIds = getSelectedTrackIds();
    if (trackIds.isEmpty()) {
        qWarning() << "No tracks selected for crate";
        return;
    }

    CrateId crateId(iCrateId);
    if (!crateId.isValid()) { // i.e. a new crate is suppose to be created
        crateId = CrateFeatureHelper(
                m_pTrackCollection, m_pConfig).createEmptyCrate();
    }
    if (crateId.isValid()) {
        m_pTrackCollection->unhideTracks(trackIds);
        m_pTrackCollection->addCrateTracks(crateId, trackIds);
    }
}

void WTrackTableView::doSortByColumn(int headerSection) {
    TrackModel* trackModel = getTrackModel();
    QAbstractItemModel* itemModel = model();

    if (trackModel == nullptr || itemModel == nullptr || !m_sorting) {
        return;
    }

    // Save the selection
    const QList<TrackId> trackIds = getSelectedTrackIds();

    sortByColumn(headerSection);

    QItemSelectionModel* currentSelection = selectionModel();

    // Find a visible column
    int visibleColumn = 0;
    while (isColumnHidden(visibleColumn) && visibleColumn < itemModel->columnCount()) {
        visibleColumn++;
    }

    currentSelection->reset(); // remove current selection

    QMap<int,int> selectedRows;
    for (const auto& trackId : trackIds) {

        // TODO(rryan) slowly fixing the issues with BaseSqlTableModel. This
        // code is broken for playlists because it assumes each trackid is in
        // the table once. This will erroneously select all instances of the
        // track for playlists, but it works fine for every other view. The way
        // to fix this that we should do is to delegate the selection saving to
        // the TrackModel. This will allow the playlist table model to use the
        // table index as the unique id instead of this code stupidly using
        // trackid.
        QLinkedList<int> rows = trackModel->getTrackRows(trackId);
        for (int row : rows) {
            // Restore sort order by rows, so the following commands will act as expected
            selectedRows.insert(row,0);
        }
    }

    QModelIndex first;
    QMapIterator<int,int> i(selectedRows);
    while (i.hasNext()) {
        i.next();
        QModelIndex tl = itemModel->index(i.key(), visibleColumn);
        currentSelection->select(tl, QItemSelectionModel::Rows | QItemSelectionModel::Select);

        if (!first.isValid()) {
            first = tl;
        }
    }

    if (first.isValid()) {
        scrollTo(first, QAbstractItemView::EnsureVisible);
        //scrollTo(first, QAbstractItemView::PositionAtCenter);
    }
}

void WTrackTableView::slotLockBpm() {
    lockBpm(true);
}

void WTrackTableView::slotUnlockBpm() {
    lockBpm(false);
}

void WTrackTableView::slotScaleBpm(int scale) {
    TrackModel* trackModel = getTrackModel();
    if (trackModel == nullptr) {
        return;
    }

    QModelIndexList selectedTrackIndices = selectionModel()->selectedRows();
    for (const auto& index : selectedTrackIndices) {
        TrackPointer track = trackModel->getTrack(index);
        if (!track->isBpmLocked()) { // bpm is not locked
            BeatsPointer beats = track->getBeats();
            if (beats != nullptr) {
                beats->scale(static_cast<Beats::BPMScale>(scale));
            } else {
                continue;
            }
        }
    }
}

void WTrackTableView::lockBpm(bool lock) {
    TrackModel* trackModel = getTrackModel();
    if (trackModel == nullptr) {
        return;
    }

    QModelIndexList selectedTrackIndices = selectionModel()->selectedRows();
    // TODO: This should be done in a thread for large selections
    for (const auto& index : selectedTrackIndices) {
        TrackPointer track = trackModel->getTrack(index);
        track->setBpmLocked(lock);
    }
}

void WTrackTableView::slotClearBeats() {
    TrackModel* trackModel = getTrackModel();
    if (trackModel == nullptr) {
        return;
    }

    QModelIndexList selectedTrackIndices = selectionModel()->selectedRows();
    // TODO: This should be done in a thread for large selections
    for (const auto& index : selectedTrackIndices) {
        TrackPointer track = trackModel->getTrack(index);
        if (!track->isBpmLocked()) {
            track->setBeats(BeatsPointer());
        }
    }
}

void WTrackTableView::slotClearWaveform() {
    TrackModel* trackModel = getTrackModel();
    if (trackModel == nullptr) {
        return;
    }

    AnalysisDao& analysisDao = m_pTrackCollection->getAnalysisDAO();
    QModelIndexList indices = selectionModel()->selectedRows();
    for (const QModelIndex& index : indices) {
        TrackPointer pTrack = trackModel->getTrack(index);
        if (!pTrack) {
            continue;
        }
        analysisDao.deleteAnalysesForTrack(pTrack->getId());
        pTrack->setWaveform(WaveformPointer());
        pTrack->setWaveformSummary(WaveformPointer());
    }
}

void WTrackTableView::slotReplayGainReset() {
    QModelIndexList indices = selectionModel()->selectedRows();
    TrackModel* trackModel = getTrackModel();

    if (trackModel == nullptr) {
        return;
    }

    for (const QModelIndex& index : indices) {
        TrackPointer pTrack = trackModel->getTrack(index);
        if (pTrack) {
            pTrack->setReplayGain(mixxx::ReplayGain());
        }
    }
}

void WTrackTableView::slotCoverInfoSelected(const CoverInfo& coverInfo) {
    TrackModel* trackModel = getTrackModel();
    if (trackModel == nullptr) {
        return;
    }
    QModelIndexList selection = selectionModel()->selectedRows();
    for (const QModelIndex& index : selection) {
        TrackPointer pTrack = trackModel->getTrack(index);
        if (pTrack) {
            pTrack->setCoverInfo(coverInfo);
        }
    }
}

void WTrackTableView::slotReloadCoverArt() {
    TrackModel* trackModel = getTrackModel();
    if (trackModel == nullptr) {
        return;
    }
    QList<TrackPointer> selectedTracks;
    QModelIndexList selection = selectionModel()->selectedRows();
    for (const QModelIndex& index : selection) {
        TrackPointer pTrack = trackModel->getTrack(index);
        if (pTrack) {
            selectedTracks.append(pTrack);
        }
    }
    CoverArtCache* pCache = CoverArtCache::instance();
    if (pCache) {
        pCache->requestGuessCovers(selectedTracks);
    }
}

bool WTrackTableView::hasFocus() const {
    return QWidget::hasFocus();
}
