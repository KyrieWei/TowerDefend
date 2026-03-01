import { writable } from 'svelte/store';

export const settingsOpen = writable(false);
export const settingsTab = writable<string>('general');

export function openSettings() {
	settingsOpen.set(true);
}

export function closeSettings() {
	settingsOpen.set(false);
}
