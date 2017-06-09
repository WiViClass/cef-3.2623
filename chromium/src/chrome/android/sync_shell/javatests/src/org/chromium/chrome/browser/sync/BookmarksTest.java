// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.sync;

import android.test.suitebuilder.annotation.LargeTest;
import android.util.Pair;

import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.Feature;
import org.chromium.chrome.browser.bookmark.BookmarksBridge;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.test.util.browser.sync.SyncTestUtil;
import org.chromium.components.bookmarks.BookmarkId;
import org.chromium.content.browser.test.util.Criteria;
import org.chromium.sync.ModelType;
import org.chromium.sync.protocol.BookmarkSpecifics;
import org.chromium.sync.protocol.SyncEntity;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Callable;

/**
 * Test suite for the bookmarks sync data type.
 */
public class BookmarksTest extends SyncTestBase {
    private static final String TAG = "BookmarksTest";

    private static final String BOOKMARKS_TYPE_STRING = "Bookmarks";

    private static final String URL = "http://chromium.org/";
    private static final String TITLE = "Chromium";
    private static final String MODIFIED_TITLE = "Chromium2";
    private static final String FOLDER_TITLE = "Tech";

    private BookmarksBridge mBookmarksBridge;

    // A container to store bookmark information for data verification.
    private static class Bookmark {
        public final String id;
        public final String title;
        public final String url;
        public final String parentId;

        private Bookmark(String id, String title, String url, String parentId) {
            this.id = id;
            this.title = title;
            this.url = url;
            this.parentId = parentId;
        }

        public boolean isFolder() {
            return url == null;
        }
    }

    private abstract class ClientBookmarksCriteria extends DataCriteria<Bookmark> {
        @Override
        public List<Bookmark> getData() throws Exception {
            return getClientBookmarks();
        }
    }

    private abstract class ServerBookmarksCriteria extends DataCriteria<Bookmark> {
        @Override
        public List<Bookmark> getData() throws Exception {
            return getServerBookmarks();
        }
    }

    @Override
    public void setUp() throws Exception {
        super.setUp();
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                mBookmarksBridge = new BookmarksBridge(Profile.getLastUsedProfile());
                // The BookmarksBridge needs to know how to handle partner bookmarks.
                // Without this call to fake that knowledge for testing, it crashes.
                mBookmarksBridge.loadEmptyPartnerBookmarkShimForTesting();
            }
        });
        setUpTestAccountAndSignInToSync();
        // Make sure initial state is clean.
        assertClientBookmarkCount(0);
        assertServerBookmarkCountWithName(0, TITLE);
        assertServerBookmarkCountWithName(0, MODIFIED_TITLE);
    }

    // Test syncing a new bookmark from server to client.
    @LargeTest
    @Feature({"Sync"})
    public void testDownloadBookmark() throws Exception {
        addServerBookmark(TITLE, URL);
        SyncTestUtil.triggerSync();
        waitForClientBookmarkCount(1);

        List<Bookmark> bookmarks = getClientBookmarks();
        assertEquals("Only the injected bookmark should exist on the client.",
                1, bookmarks.size());
        Bookmark bookmark = bookmarks.get(0);
        assertEquals("The wrong title was found for the bookmark.", TITLE, bookmark.title);
        assertEquals("The wrong URL was found for the bookmark.", URL, bookmark.url);
    }

    // Test syncing a bookmark modification from server to client.
    @LargeTest
    @Feature({"Sync"})
    public void testDownloadBookmarkModification() throws Exception {
        // Add the entity to test modifying.
        addServerBookmark(TITLE, URL);
        SyncTestUtil.triggerSync();
        waitForClientBookmarkCount(1);

        // Modify on server, sync, and verify the modification locally.
        Bookmark bookmark = getClientBookmarks().get(0);
        modifyServerBookmark(bookmark.id, MODIFIED_TITLE, URL);
        SyncTestUtil.triggerSync();
        pollForCriteria(new ClientBookmarksCriteria() {
            @Override
            public boolean isSatisfied(List<Bookmark> bookmarks) {
                Bookmark modifiedBookmark = bookmarks.get(0);
                return modifiedBookmark.title.equals(MODIFIED_TITLE);
            }
        });
    }

    // Test syncing a bookmark tombstone from server to client.
    @LargeTest
    @Feature({"Sync"})
    public void testDownloadBookmarkTombstone() throws Exception {
        // Add the entity to test deleting.
        addServerBookmark(TITLE, URL);
        SyncTestUtil.triggerSync();
        waitForClientBookmarkCount(1);

        // Delete on server, sync, and verify deleted locally.
        Bookmark bookmark = getClientBookmarks().get(0);
        mFakeServerHelper.deleteEntity(bookmark.id);
        SyncTestUtil.triggerSync();
        waitForClientBookmarkCount(0);
    }

    // Test syncing a bookmark modification from server to client.
    @LargeTest
    @Feature({"Sync"})
    public void testDownloadMovedBookmark() throws Exception {
        // Add the entity to test moving.
        addServerBookmark(TITLE, URL);
        SyncTestUtil.triggerSync();
        waitForClientBookmarkCount(1);

        // Add the folder to move to.
        addServerBookmarkFolder(FOLDER_TITLE);
        SyncTestUtil.triggerSync();
        waitForClientBookmarkCount(2);

        // The folder comes first because new entities are inserted at index 0.
        final Bookmark folder = getClientBookmarks().get(0);
        Bookmark bookmark = getClientBookmarks().get(1);
        assertTrue(folder.isFolder());
        assertFalse(bookmark.isFolder());

        // Move on server, sync, and verify the move locally.
        mFakeServerHelper.modifyBookmarkEntity(bookmark.id, TITLE, URL, folder.id);
        SyncTestUtil.triggerSync();
        pollForCriteria(new ClientBookmarksCriteria() {
            @Override
            public boolean isSatisfied(List<Bookmark> bookmarks) {
                // The "s" is prepended because the server adds one to the parentId.
                return bookmarks.get(1).parentId.equals("s" + folder.id);
            }
        });
    }

    // Test syncing a new bookmark folder from server to client.
    @LargeTest
    @Feature({"Sync"})
    public void testDownloadBookmarkFolder() throws Exception {
        addServerBookmarkFolder(TITLE);
        SyncTestUtil.triggerSync();
        waitForClientBookmarkCount(1);

        List<Bookmark> bookmarks = getClientBookmarks();
        assertEquals("Only the injected bookmark folder should exist on the client.", 1,
                bookmarks.size());
        Bookmark folder = bookmarks.get(0);
        assertEquals("The wrong title was found for the folder.", TITLE, folder.title);
        assertEquals("Bookmark folders do not have a URL.", null, folder.url);
    }

    // Test syncing a bookmark folder modification from server to client.
    @LargeTest
    @Feature({"Sync"})
    public void testDownloadBookmarkFolderModification() throws Exception {
        // Add the entity to test modifying.
        addServerBookmarkFolder(TITLE);
        SyncTestUtil.triggerSync();
        waitForClientBookmarkCount(1);

        // Modify on server, sync, and verify the modification locally.
        Bookmark folder = getClientBookmarks().get(0);
        assertTrue(folder.isFolder());
        modifyServerBookmarkFolder(folder.id, MODIFIED_TITLE);
        SyncTestUtil.triggerSync();

        pollForCriteria(new ClientBookmarksCriteria() {
            @Override
            public boolean isSatisfied(List<Bookmark> bookmarks) {
                Bookmark modifiedFolder = bookmarks.get(0);
                return modifiedFolder.isFolder() && modifiedFolder.title.equals(MODIFIED_TITLE);
            }
        });
    }

    // Test syncing a bookmark folder tombstone from server to client.
    @LargeTest
    @Feature({"Sync"})
    public void testDownloadBookmarkFolderTombstone() throws Exception {
        // Add the entity to test deleting.
        addServerBookmarkFolder(TITLE);
        assertServerBookmarkCountWithName(1, TITLE);
        SyncTestUtil.triggerSync();
        waitForClientBookmarkCount(1);

        // Delete on server, sync, and verify deleted locally.
        Bookmark folder = getClientBookmarks().get(0);
        assertTrue(folder.isFolder());

        mFakeServerHelper.deleteEntity(folder.id);
        assertServerBookmarkCountWithName(0, TITLE);
        SyncTestUtil.triggerSync();
        waitForClientBookmarkCount(0);
    }

    // Test syncing a new bookmark from client to server.
    @LargeTest
    @Feature({"Sync"})
    public void testUploadBookmark() throws Exception {
        addClientBookmark(TITLE, URL);
        waitForClientBookmarkCount(1);
        waitForServerBookmarkCountWithName(1, TITLE);
    }

    // Test syncing a bookmark modification from client to server.
    @LargeTest
    @Feature({"Sync"})
    public void testUploadBookmarkModification() throws Exception {
        // Add the entity to test modifying.
        BookmarkId bookmarkId = addClientBookmark(TITLE, URL);
        waitForClientBookmarkCount(1);
        waitForServerBookmarkCountWithName(1, TITLE);

        setClientBookmarkTitle(bookmarkId, MODIFIED_TITLE);
        waitForServerBookmarkCountWithName(1, MODIFIED_TITLE);
        assertServerBookmarkCountWithName(0, TITLE);
    }

    // Test syncing a bookmark tombstone from client to server.
    @LargeTest
    @Feature({"Sync"})
    public void testUploadBookmarkTombstone() throws Exception {
        // Add the entity to test deleting.
        BookmarkId bookmarkId = addClientBookmark(TITLE, URL);
        waitForClientBookmarkCount(1);
        waitForServerBookmarkCountWithName(1, TITLE);

        deleteClientBookmark(bookmarkId);
        waitForClientBookmarkCount(0);
        waitForServerBookmarkCountWithName(0, TITLE);
        assertServerBookmarkCountWithName(0, MODIFIED_TITLE);
    }

    // Test syncing a bookmark modification from client to server.
    @LargeTest
    @Feature({"Sync"})
    public void testUploadMovedBookmark() throws Exception {
        // Add the entity to test moving.
        BookmarkId bookmarkId = addClientBookmark(TITLE, URL);
        SyncTestUtil.triggerSync();
        waitForServerBookmarkCountWithName(1, TITLE);

        // Add the folder to move to.
        BookmarkId folderId = addClientBookmarkFolder(FOLDER_TITLE);
        SyncTestUtil.triggerSync();
        waitForServerBookmarkCountWithName(1, FOLDER_TITLE);

        List<Bookmark> bookmarks = getServerBookmarks();
        assertEquals("Wrong number of bookmarks.", 2, bookmarks.size());
        final Bookmark folder = bookmarks.get(bookmarks.get(0).isFolder() ? 0 : 1);

        // Move on client, sync, and verify the move on the server.
        moveClientBookmark(bookmarkId, folderId);
        SyncTestUtil.triggerSync();
        pollForCriteria(new ServerBookmarksCriteria() {
            @Override
            public boolean isSatisfied(List<Bookmark> bookmarks) {
                Bookmark modifiedBookmark = bookmarks.get(bookmarks.get(0).isFolder() ? 1 : 0);
                return modifiedBookmark.parentId.equals(folder.id);
            }
        });
    }

    // Test syncing a new bookmark folder from client to server.
    @LargeTest
    @Feature({"Sync"})
    public void testUploadBookmarkFolder() throws Exception {
        addClientBookmarkFolder(TITLE);
        waitForClientBookmarkCount(1);
        waitForServerBookmarkCountWithName(1, TITLE);
    }

    // Test syncing a bookmark folder modification from client to server.
    @LargeTest
    @Feature({"Sync"})
    public void testUploadBookmarkFolderModification() throws Exception {
        // Add the entity to test modifying.
        BookmarkId bookmarkId = addClientBookmarkFolder(TITLE);
        waitForClientBookmarkCount(1);
        waitForServerBookmarkCountWithName(1, TITLE);

        setClientBookmarkTitle(bookmarkId, MODIFIED_TITLE);
        waitForServerBookmarkCountWithName(1, MODIFIED_TITLE);
        assertServerBookmarkCountWithName(0, TITLE);
    }

    // Test syncing a bookmark folder tombstone from client to server.
    @LargeTest
    @Feature({"Sync"})
    public void testUploadBookmarkFolderTombstone() throws Exception {
        // Add the entity to test deleting.
        BookmarkId bookmarkId = addClientBookmarkFolder(TITLE);
        waitForClientBookmarkCount(1);
        waitForServerBookmarkCountWithName(1, TITLE);

        deleteClientBookmark(bookmarkId);
        waitForClientBookmarkCount(0);
        waitForServerBookmarkCountWithName(0, TITLE);
    }

    // Test that bookmarks don't get downloaded if the data type is disabled.
    @LargeTest
    @Feature({"Sync"})
    public void testDisabledNoDownloadBookmark() throws Exception {
        disableDataType(ModelType.BOOKMARKS);
        addServerBookmark(TITLE, URL);
        SyncTestUtil.triggerSyncAndWaitForCompletion();
        assertClientBookmarkCount(0);
    }

    // Test that bookmarks don't get uploaded if the data type is disabled.
    @LargeTest
    @Feature({"Sync"})
    public void testDisabledNoUploadBookmark() throws Exception {
        disableDataType(ModelType.BOOKMARKS);
        addClientBookmark(TITLE, URL);
        SyncTestUtil.triggerSyncAndWaitForCompletion();
        assertServerBookmarkCountWithName(0, TITLE);
    }

    private BookmarkId addClientBookmark(final String title, final String url) {
        BookmarkId id = ThreadUtils.runOnUiThreadBlockingNoException(new Callable<BookmarkId>() {
            @Override
            public BookmarkId call() throws Exception {
                BookmarkId parentId = mBookmarksBridge.getMobileFolderId();
                return mBookmarksBridge.addBookmark(parentId, 0, title, url);
            }
        });
        assertNotNull("Failed to create bookmark.", id);
        return id;
    }

    private BookmarkId addClientBookmarkFolder(final String title) {
        BookmarkId id = ThreadUtils.runOnUiThreadBlockingNoException(new Callable<BookmarkId>() {
            @Override
            public BookmarkId call() throws Exception {
                BookmarkId parentId = mBookmarksBridge.getMobileFolderId();
                return mBookmarksBridge.addFolder(parentId, 0, title);
            }
        });
        assertNotNull("Failed to create bookmark folder.", id);
        return id;
    }

    private void addServerBookmark(String title, String url) throws InterruptedException {
        mFakeServerHelper.injectBookmarkEntity(
                title, url, mFakeServerHelper.getBookmarkBarFolderId());
    }

    private void addServerBookmarkFolder(String title) throws InterruptedException {
        mFakeServerHelper.injectBookmarkFolderEntity(
                title, mFakeServerHelper.getBookmarkBarFolderId());
    }

    private void modifyServerBookmark(String bookmarkId, String title, String url) {
        mFakeServerHelper.modifyBookmarkEntity(
                bookmarkId, title, url, mFakeServerHelper.getBookmarkBarFolderId());
    }

    private void modifyServerBookmarkFolder(String folderId, String title) {
        mFakeServerHelper.modifyBookmarkFolderEntity(
                folderId, title, mFakeServerHelper.getBookmarkBarFolderId());
    }

    private void deleteClientBookmark(final BookmarkId id) {
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                mBookmarksBridge.deleteBookmark(id);
            }
        });
    }

    private void setClientBookmarkTitle(final BookmarkId id, final String title) {
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                mBookmarksBridge.setBookmarkTitle(id, title);
            }
        });
    }

    private void moveClientBookmark(final BookmarkId id, final BookmarkId newParentId) {
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                mBookmarksBridge.moveBookmark(id, newParentId, 0 /* new index */);
            }
        });
    }

    private List<Bookmark> getClientBookmarks() throws JSONException {
        List<Pair<String, JSONObject>> rawBookmarks = SyncTestUtil.getLocalData(
                mContext, BOOKMARKS_TYPE_STRING);
        List<Bookmark> bookmarks = new ArrayList<Bookmark>(rawBookmarks.size());
        for (Pair<String, JSONObject> rawBookmark : rawBookmarks) {
            String id = rawBookmark.first;
            JSONObject json = rawBookmark.second;
            bookmarks.add(new Bookmark(id, json.getString("title"), json.optString("url", null),
                    json.getString("parent_id")));
        }
        return bookmarks;
    }

    private List<Bookmark> getServerBookmarks() throws Exception {
        List<SyncEntity> entities =
                mFakeServerHelper.getSyncEntitiesByModelType(ModelType.BOOKMARKS);
        List<Bookmark> bookmarks = new ArrayList<Bookmark>(entities.size());
        for (SyncEntity entity : entities) {
            String id = entity.idString;
            String parentId = entity.parentIdString;
            BookmarkSpecifics specifics = entity.specifics.bookmark;
            bookmarks.add(new Bookmark(id, specifics.title, specifics.url, parentId));
        }
        return bookmarks;
    }

    private void assertClientBookmarkCount(int count) throws JSONException {
        assertEquals("There should be " + count + " local bookmarks.",
                count, SyncTestUtil.getLocalData(mContext, BOOKMARKS_TYPE_STRING).size());
    }

    private void assertServerBookmarkCountWithName(int count, String name) {
        assertTrue("There should be " + count + " remote bookmarks with name " + name + ".",
                mFakeServerHelper.verifyEntityCountByTypeAndName(
                            count, ModelType.BOOKMARKS, name));
    }

    private void waitForClientBookmarkCount(final int n) throws InterruptedException {
        pollForCriteria(new Criteria("There should be " + n + " local bookmarks.") {
            @Override
            public boolean isSatisfied() {
                try {
                    return SyncTestUtil.getLocalData(mContext, BOOKMARKS_TYPE_STRING).size() == n;
                } catch (Exception e) {
                    throw new RuntimeException(e);
                }
            }
        });
    }

    private void waitForServerBookmarkCountWithName(final int count, final String name)
            throws InterruptedException {
        pollForCriteria(new Criteria(
                "Expected " + count + " remote bookmarks with name " + name + ".") {
            @Override
            public boolean isSatisfied() {
                try {
                    return mFakeServerHelper.verifyEntityCountByTypeAndName(
                            count, ModelType.BOOKMARKS, name);
                } catch (Exception e) {
                    throw new RuntimeException(e);
                }
            }
        });
    }
}
