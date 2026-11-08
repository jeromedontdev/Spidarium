// Timing and round transitions match main/ui_menu.c; time is injected in ms.
export const GAME_NAMES = ['Bug Hunt', 'Color Match', 'Quick Tap'];
export const COLOR_NAMES = ['Coral', 'Mint', 'Gold', 'Blue'];
export const COLOR_RGB = ['#b94646', '#296e55', '#846421', '#315c94'];
const roll = (random, n) => Math.floor(random() * n);
export class Minigame {
  constructor(kind, now, random = Math.random) {
    if (!Number.isInteger(kind) || kind < 0 || kind > 2) throw new Error('Unknown minigame');
    this.kind = kind; this.random = random; this.round = 0; this.score = 0;
    this.finished = false; this.cancelled = false; this.startRound(now);
  }
  startRound(now) {
    this.answer = roll(this.random, 4); this.ready = this.kind !== 2;
    this.pending = false; this.preview = this.kind === 1;
    this.duration = this.kind === 2 ? 620 - this.round * 45 : 2000 - this.round * 110;
    this.feints = 0; this.length = 1 + Math.floor(this.round / 3);
    this.step = 0; this.previewStep = 0; this.inputGuard = 0;
    this.sequence = [this.answer];
    for (let i = 1; i < this.length; i++) this.sequence.push(roll(this.random, 4));
    this.bug = roll(this.random, 8);
    this.bugs = Array.from({ length: 4 }, (_, i) => (this.bug + i + 1) % 8);
    this.bugs[this.answer] = this.bug;
    // Shuffle the decoys too, so the answer is not always the only unique species.
    for (let i = 3; i > 0; i--) {
      const j = roll(this.random, i + 1);
      [this.bugs[i], this.bugs[j]] = [this.bugs[j], this.bugs[i]];
    }
    this.answer = this.bugs.indexOf(this.bug);
    this.colors = [0, 1, 2, 3];
    for (let i = 3; i > 0; i--) {
      const j = roll(this.random, i + 1);
      [this.colors[i], this.colors[j]] = [this.colors[j], this.colors[i]];
    }
    this.deadline = now + (this.preview ? 750 : this.kind === 2 ? 900 + roll(this.random, 1600) : this.duration);
    this.shiftAt = now + 1500 - this.round * 60;
    this.feedback = '';
  }
  endRound(correct, now) {
    if (correct) this.score++;
    this.round++;
    this.feedback = correct ? 'Nice!' : 'Try Again';
    if (this.round === 8) this.finished = true;
    else { this.pending = true; this.deadline = now + 600; }
  }
  answerAt(index, now) {
    if (this.finished || this.cancelled || this.pending || this.preview || now < this.inputGuard) return false;
    if (!Number.isInteger(index) || index < 0 || index > 3) return false;
    const inTime = now < this.deadline;
    if (this.kind === 1 && index === this.answer && inTime) {
      this.step++;
      if (this.step < this.length) {
        this.answer = this.sequence[this.step]; this.inputGuard = now + 150;
        this.feedback = 'Good / Next Color...'; return true;
      }
    }
    this.endRound(this.kind === 2 ? this.ready && inTime : index === this.answer && inTime, now);
    return true;
  }
  tick(now) {
    if (this.finished || this.cancelled) return;
    if (this.kind === 0 && !this.pending && now >= this.shiftAt && now < this.deadline) {
      const other = (this.answer + 1 + roll(this.random, 3)) % 4;
      [this.bugs[this.answer], this.bugs[other]] = [this.bugs[other], this.bugs[this.answer]];
      this.answer = other; this.shiftAt = now + 1500 - this.round * 60;
    }
    if (now < this.deadline) return;
    if (this.pending) this.startRound(now);
    else if (this.preview) {
      this.previewStep++;
      if (this.previewStep < this.length) this.deadline = now + 750;
      else { this.preview = false; this.deadline = now + this.duration; }
    } else if (this.kind === 2 && !this.ready) {
      if (this.feints < (this.round >= 2 ? 2 : 1)) {
        this.feints++; this.deadline = now + 400 + roll(this.random, 700);
      } else { this.ready = true; this.deadline = now + this.duration; }
    } else this.endRound(false, now);
  }
  cancel() { this.cancelled = true; }
  remaining(now) {
    return this.ready && !this.preview && !this.pending ? Math.max(0, (this.deadline - now) / this.duration) : 1;
  }
  prompt(species) {
    if (this.pending) return this.feedback;
    if (this.feedback) return this.feedback;
    const prefix = `Round ${this.round + 1}/8 / Score ${this.score}\n`;
    if (this.kind === 0) return `${prefix}Find: ${species[this.bug]}`;
    if (this.kind === 1) return this.preview
      ? `${prefix}Remember ${this.previewStep + 1}/${this.length}: ${COLOR_NAMES[this.colors[this.sequence[this.previewStep]]]}`
      : this.length > 1 ? 'Repeat the Colors in Order' : 'Match the Color You Remembered';
    return this.ready ? 'Tap Now!' : `${prefix}Only Tap GREEN`;
  }
}
