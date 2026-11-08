export const STORAGE_KEY = 'spidarium.browser-habitat.v1';

export class LocalProgress {
  constructor(storage, restore, report) {
    this.storage = storage;
    this.restore = restore;
    this.report = report;
    this.blocked = false;
  }
  load() {
    try {
      const text = this.storage.getItem(STORAGE_KEY);
      return this.restore(text === null ? null : JSON.parse(text));
    } catch (error) {
      this.blocked = true;
      this.report(`Local save could not be loaded: ${error.message}. It has not been overwritten.`);
      return this.restore(null);
    }
  }
  save(state) {
    if (this.blocked) return false;
    try {
      this.storage.setItem(STORAGE_KEY, JSON.stringify(state));
      return true;
    } catch (error) {
      this.blocked = true;
      this.report(`Progress cannot be saved locally: ${error.message}. Enable site storage or download a backup.`);
      return false;
    }
  }
  reset(state) {
    try {
      this.storage.setItem(STORAGE_KEY, JSON.stringify(state));
      this.blocked = false;
      return true;
    } catch (error) {
      this.report(`Local save could not be replaced: ${error.message}. Existing progress is unchanged.`);
      return false;
    }
  }
}
