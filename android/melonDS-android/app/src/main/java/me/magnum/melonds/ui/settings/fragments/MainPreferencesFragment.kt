package me.magnum.melonds.ui.settings.fragments

import android.os.Bundle
import android.graphics.Rect
import android.view.View
import androidx.core.view.updatePadding
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.RecyclerView
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

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        // Sized so that the whole home fits the Thor's top screen without scrolling. The first and last cards span two
        // columns so that the ten cards fill complete rows.
        val widthDp = resources.configuration.screenWidthDp
        val columns = when {
            widthDp >= 720 -> 4
            widthDp >= 360 -> 2
            else -> 1
        }
        val gap = (10 * resources.displayMetrics.density).toInt()
        if (widthDp >= 600) {
            listView.updatePadding(left = 2 * gap, right = 2 * gap)
        }
        listView.layoutManager = GridLayoutManager(requireContext(), columns).apply {
            spanSizeLookup = object : GridLayoutManager.SpanSizeLookup() {
                override fun getSpanSize(position: Int): Int {
                    val isEdge = position == 0 || position == preferenceScreen.preferenceCount - 1
                    return if (isEdge) minOf(2, columns) else 1
                }
            }
        }
        listView.addItemDecoration(object : RecyclerView.ItemDecoration() {
            override fun getItemOffsets(outRect: Rect, view: View, parent: RecyclerView, state: RecyclerView.State) {
                val params = view.layoutParams as GridLayoutManager.LayoutParams
                val start = params.spanIndex * gap / columns
                val end = gap - (params.spanIndex + params.spanSize) * gap / columns
                if (parent.layoutDirection == View.LAYOUT_DIRECTION_RTL) {
                    outRect.left = end
                    outRect.right = start
                } else {
                    outRect.left = start
                    outRect.right = end
                }
            }
        })
    }
}
