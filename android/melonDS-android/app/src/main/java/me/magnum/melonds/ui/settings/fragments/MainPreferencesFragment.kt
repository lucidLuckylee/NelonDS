package me.magnum.melonds.ui.settings.fragments

import android.os.Bundle
import androidx.preference.PreferenceFragmentCompat
import dagger.hilt.android.AndroidEntryPoint
import me.magnum.melonds.R
import me.magnum.melonds.ui.settings.PreferenceFragmentTitleProvider

@AndroidEntryPoint
class MainPreferencesFragment : BasePreferenceFragment(), PreferenceFragmentTitleProvider {

    override fun getTitle() = getString(R.string.settings)

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        setPreferencesFromResource(R.xml.pref_main, rootKey)
    }

    // Sized so that the whole home fits the Thor's top screen without scrolling. The first and last cards span two
    // columns so that the ten cards fill complete rows.
    override fun columnCount(widthDp: Int) = when {
        widthDp >= 720 -> 4
        widthDp >= 360 -> 2
        else -> 1
    }

    override fun spanSize(position: Int, itemCount: Int, columns: Int): Int {
        val isEdge = position == 0 || position == itemCount - 1
        return if (isEdge) minOf(2, columns) else 1
    }
}
